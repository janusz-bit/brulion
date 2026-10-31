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
* **Appearance settings** (Settings → **Wygląd**, device-local in `AppSettings`; a phone and a
  desktop want different looks): the theme mode; the **accent colour** — presets Czerwony (the
  margin red, default), Niebieski, Zielony, Morski, Fioletowy, Grafitowy, or **Własny…** (any
  `#rrggbb` from `BAccentDialog`: a 12 × 3 colour grid, a hex field, a live preview);
  `ThemePalette` (C++) derives readable shades by itself — the accent as a mark ≥ 3:1, as text
  ≥ 4.5:1 on every surface and on its own tint, the solid fill ≥ 4.5:1 under white text. The
  accent marks things (today, the current tab, the selected row's bar, drop targets); it never
  means "destructive": danger callouts, danger cards, deleting stay red. The **light
  background** Papier (default) | Białe (neutral white, grey wells) and the **dark background**
  Granatowe (default) | Czarne (OLED: true black, barely lifted cards); both rows show while
  the mode is "System". **Text size** (UI factor), the **interface font** (any installed family,
  `BFontPickerDialog`; "" = the system's), **density** Wygodna | Zwarta (desktop only: 24/30/36
  px controls, 28 px rows, 26 px sidebar rows, a 248 px sidebar, a smaller paragraph gap; touch
  UIs keep 44 px targets and never show the row), reduce motion. **Układ**: the sidebar's
  entries (Dziennik, Kalendarz, Zadania, Wszystkie strony, Tagi, Graf, Szukaj) and its sections
  Ulubione and Ostatnie can be unticked (the "Strony" tree stays; hidden views stay in the
  command palette and their shortcuts); the phone's **bottom navigation**: four buttons, each
  from a menu of Dziennik, Kalendarz, Szukaj, Zadania, Strony, Tagi, Graf (a view already in
  another slot swaps places with it), Menu always last; the **quick note button** on or off;
  "Przywróć domyślny wygląd" (colours, fonts, sizes, density, line spacing — the visibility of
  elements stays). A family chosen on another device or since uninstalled falls back to the
  default font. The browser has no font rows (Qt there sees only its bundled DejaVu fonts).
* Typography: system sans for UI (or a chosen family); the editor font is serif, sans or any
  installed family (Settings → Edytor → Czcionka → **Inna…**), the code font any monospaced
  family (code blocks, source mode, formulas, calculations; inline `code` in rendered text keeps
  Qt's own fixed font); line spacing Ciasny 1.45 | Normalny 1.65 | Luźny 1.9; configurable max
  text width and text size.
* Outliner look like Logseq: round bullets, thin vertical guide lines per nesting level, fold
  arrows, clicking a bullet zooms into the block (breadcrumb to zoom out; Enter in a zoomed
  heading or paragraph ends the zoom) — in outline mode; in document mode a note reads like an
  Obsidian page (see "Note modes" below).
* Touch (Android, `--touch`): targets ≥ 44 px, bottom navigation, drawer, an editing toolbar
  docked on the on-screen keyboard (the bottom navigation makes way while the keyboard is up;
  when it goes, the navigation comes back and a toolbar that stays docks right on it), dialogs as
  bottom sheets on the keyboard ("On-screen keyboard" below).

## Window layout (`qml/Main.qml`, `qml/shell/AppShell.qml`)

* **Wide (≥ 900 px):** `SplitView` — left **sidebar** | **top bar** + current view | optional
  right **side panel**.
  * Sidebar (`ShellSidebar`): the notebook's name, the **Przełącz notatnik** button (the
    `repeat` icon; a menu of the recent notebooks — the current one checked, one click switches
    — then „Dokumentacja Brulionu” and „Otwórz inny notatnik…” (the start screen); a recent
    notebook whose folder is gone is not recreated: a warning, and it leaves the list) and a
    new-page button; navigation Dziennik,
    Kalendarz, Zadania, Wszystkie strony, Tagi, Graf, Szukaj; Ulubione; Ostatnie; Strony (the
    folder tree — see "Folders" below); a sync status footer (opens the sync center).
  * Top bar (`ShellTopBar`): sidebar toggle, back / forward, the title (a breadcrumb for pages
    in folders), Szukaj, Paleta poleceń, Szybka notatka, side panel toggle and **Więcej** (⋮).
  * Side panel (`ShellSidePanel`): a page opened "in the side panel", or the current view's
    context — for pages and journals the tabs **Linki** (linked references and unlinked
    mentions), **Konspekt** (headings: a click scrolls to one, a right-click / long press copies
    its link `[[Strona#Nagłówek]]` or its embed), **Kalendarz** (month with journal days).
* **Narrow (< 900 px, phones):** the sidebar becomes a `Drawer` (the ☰ button, the bottom bar's
  Menu, or a swipe to the right from the left edge; a swipe to the left on the drawer or the
  scrim closes it). On touch UIs the swipe starts in a 24 px strip (`drawer.dragMargin`) that
  begins beyond what the system keeps at that edge: Android's gesture navigation takes a swipe
  from the very edge for "back" (`GestureInsets.left`, the system gesture insets reported by
  `ImeInsets.kt`, ~30 dp; 0 with button navigation), a display cutout or a system bar in
  landscape its `SafeArea` margin — so with gesture navigation the finger starts a little in
  from the edge. Taps and vertical scrolls in the strip reach the content; only a mostly
  horizontal drag past the drawer's 20 px threshold opens it, not one an item already keeps (a
  scrolling Flickable); Android's text selection handles are windows of their own above the app.
  While typing or selecting blocks (the keyboard up or an editing toolbar docked, also on the
  bottom navigation after the keyboard went) there is no strip: the drawer took the drag of the
  selection's start grip, which lies at the left edge (☰ still opens it, a swipe on it still
  closes it).
  Nor on a view that swipes sideways from the left edge itself (the view's `edgeSwipe`,
  `ShellRouteView.viewEdgeSwipe`): the calendar's week and month grids reach that edge and a
  swipe to the right there shows the previous period; the agenda keeps the strip. While the
  drawer is open the shell marks the view `covered` and the grids' swipe is off, so a swipe on
  the drawer closes it instead of turning the period underneath. In a mobile browser
  (WebAssembly) nothing reports the system's zones, so the strip lies at the very edge, where
  Android's gesture navigation or the browser's own edge swipe (iOS Safari) may take the swipe
  for "back" first — ☰ and the bottom bar's Menu open the drawer there. The strip follows the
  UI mode (`Theme.touchUi`), not the pointer: a mouse or touchpad in a touch UI (a narrow window
  on a laptop with a touch screen, Android with a mouse) gets the 24 px strip too, outside a
  touch UI it is Qt's default (the start-drag distance); the top bar keeps ☰, back (when there is history), title, Szukaj and Więcej;
  a **bottom navigation** bar (by default Dziennik, Kalendarz, Szukaj, Zadania — four views
  chosen in Settings → Wygląd → Układ — and Menu → drawer); a floating quick-capture button
  (hidden while editing, or by the setting). The navigation makes way while the on-screen
  keyboard is up — when the keyboard goes while the editing goes on, it comes back and the
  editing toolbar docks right on it; a tap on one of its tabs then opens that view, the editing
  ends as when leaving the page —, and for the drawing editor's tool bar, which takes its place;
  the capture button makes way while typing: the keyboard up, or an editing toolbar docked
  ("On-screen keyboard"). The Android back key /
  gesture walks back
  through the history; the app closes only from the home view. The drawer also lists
  **Importuj notatki** under the navigation (the import tab of `ImportExportView`).
* **Więcej menu** (top bar ⋮): on a page or daily note — Dodaj do ulubionych / Usuń z
  ulubionych, Zmień nazwę… (F2) and Przenieś do… (on a page route — a daily note opened as a page
  too —, not in the journal views by date: "Folders" below), Pokaż w drzewie (the
  sidebar, the drawer on phones), Historia strony, Źródło Markdown (Ctrl+E), Przełącz
  tryb notatki, Otwórz w panelu bocznym (wide only), Przenieś do kosza — on a picture attachment
  Źródło Markdown is the picture bar's Pokaż jako tekst / Pokaż obraz and the note mode is not
  offered (nor in the palette: a picture is no note); always — Nowa strona (Ctrl+N), Paleta
  poleceń (Ctrl+K), **Centrum synchronizacji, Importuj notatki (the import tab), Import i eksport,
  Kosz (n), Ustawienia (Ctrl+,), O Brulionie**. These destinations have no button of their own: on
  phones this menu (and the command palette) is the way to them. A new notebook (at most one
  note) also offers **Importuj notatki** under the empty journal's hint, and onboarding has an
  "Importuj notatki" card that opens the chosen notebook on its import tab
  (`AppController.openVaultAt`); outside the browser its text first says that an Obsidian vault
  (also one synced with Syncthing) needs no import: its folder opens as the notebook
  (docs/IMPORT.md "Folder Obsidiana bez importu").
* **The documentation notebook** „Dokumentacja Brulionu” (desktop and Android; the browser has
  one notebook, so its sample welcome page links to the guide online): Brulion notes written
  from `src/app/shell/guide/*.md` into `<AppData>/Dokumentacja Brulionu` and brought up to date
  on every open (pages the user changed or deleted stay so). It opens on its table of contents
  from the switcher, the palette („Dokumentacja Brulionu” / „Wróć do mojego notatnika”) or
  Ctrl+Shift+B, and is for reading: it is never a recent or start notebook (a restart opens the
  user's last notebook; the start drops it from the recent notebooks where 0.3.0–0.3.5 listed
  it), arms no reminders (the user's notebook keeps its armed alarms), quick
  capture and reminder taps from it go back to the user's notebook, and its Nextcloud switch
  is its own (off; it never takes over the user notebook's server folder).
* Shell popups: command palette (`BCommandPalette` over `CommandModel`: navigation, journals,
  new page, Nowy folder…, Przenieś do…, Pokaż w drzewie, capture, theme, language, sync, panels,
  settings, about; the editor's commands —
  Markdown source, note mode, "Kopiuj link do strony", "Kopiuj odwołanie do bloku", "Kopiuj link
  do bloku", "Kopiuj link do nagłówka": the current block is the one being edited (a table whose
  cell is edited too; a cell left for the palette leaves its table selected), else the selected
  one, the heading that of its section; nothing current: a toast "Najpierw zaznacz blok lub umieść
  w nim kursor"; in Markdown source the three block and heading commands are disabled — the
  page's views report it as `sourceMode`, AppShell binds `AppController.sourceModeShown`), quick switcher (`BQuickSwitcher`, fuzzy page search), quick capture (one
  line → today's journal; dates in the text become ⏰ / 📅; Android's home-screen widgets have a
  quick add of their own, outside the QML UI — date chips instead of words, docs/ANDROID.md
  "Widżety"), the share dialog
  (`ShellShareDialog`, "Zapisz w Brulionie": text or `.md`/`.txt` files another app shared —
  Android's share sheet, docs/ANDROID.md "Udostępnij → Brulion", the browser version's share
  target (docs/WEB.md "Udostępnianie do Brulionu": `--share-query`, or handed over by another tab
  through `WasmShare` to `AppController.receiveShareQuery`), a desktop's "Otwórz za pomocą →
  Brulion" (`--open <file>…`, a second launch's `share` message: `pendingShare.source` "open" —
  the subtitle says "%n plik(i) do zapisania w notatniku" and the hint `shareCopyHint` that a copy
  is saved and the file stays unchanged; a file that already is a page of the open notebook — also
  in a folder linked into it, a page that is a link, in a folder starting with "." — opens as that
  page instead, the dialog lists only the others; opened files arriving while the dialog waits are
  added to it, a file opened again is read again, any other share replaces it), or `--share` — go
  to today's journal or to new pages; `AppController.pendingShare` / `saveShare` / `dismissShare`; a
  segmented "Dzisiejszy dziennik" / "Nowa strona" (text: the journal by default, one file: a new
  page titled after the file, several: one journal block each), the page title field for a single
  page, the text editable before saving — never through quick capture's date parsing —, files
  listed by name with their first lines or the reason they cannot be saved, all as plain text; a
  refused save (a page whose name differs only in letter case exists) keeps the dialog open with
  the reason; "Anuluj" drops the share and its copies; `requestUiAction("share")` opens it once
  a user notebook is ready), new page / folder, rename, move
  (`ShellMoveDialog`), delete confirmation, about; toasts. Views reach the shell's dialogs for a
  page or folder through `AppController.requestItemAction(action, path)` (`move`, `rename`,
  `delete`, `newPage`, `newFolder`, `reveal`).
* **The tray calendar** (`ShellTrayCalendar`, desktop; docs/WINDOWS.md "Kalendarz z zasobnika"):
  a window of its own (Qt.Tool, frameless, always on top, no transient parent — owned by a
  hidden main window it would hide with it on Windows), created on first use by Main.qml's
  `Loader`, about 336 × 500 px with a 1 px frame: today's long date and a close button,
  `ContentMiniCalendar` (the month with its dots, the picked day's tasks and reminders, "Otwórz
  dziennik"; reset to today's month on every open; without a notebook only the month and
  „Otwórz notatnik, aby zobaczyć zadania i przypomnienia.”), "Otwórz Brulion" and "Szybka
  notatka…". An entry or "Otwórz dziennik" navigates and calls `AppController.showWindow()`,
  which hides the calendar (Main.qml hides it on every `showWindowRequested`, on "toggleWindow"
  and before the main window closes, so a shown calendar never keeps a really closed app
  running). It opens from the tray (`DesktopTray`): the menu's „Kalendarz” (`uiActionRequested
  ("trayCalendar")`) and, with Ustawienia → Aplikacja → „Kliknięcie ikony w zasobniku” =
  Kalendarz (`AppSettings.trayClick`, `app/trayClick`: "calendar" by default on Windows, "window"
  elsewhere), a single left click ("toggleTrayCalendar": an open calendar closes); a double click
  always shows the window. Placement: `AppController.trayPopupPosition(w, h)` →
  `traypopup.h` — against the work area's edge nearest to the tray icon
  (`QSystemTrayIcon::geometry()`, Windows) or to the cursor (X11, where the icon's rectangle is
  empty), centred on it along that edge, 8 px away, kept inside the work area of the anchor's
  screen; Wayland ignores it (the compositor places the window). It hides itself — always
  `hide()`, never a close: `onClosing` refuses Alt+F4's close — on Escape, the close button, and
  when it loses the activation after having had it (a click elsewhere; a refused activation keeps
  it open without the keyboard — no Escape — until the close button, an entry or a click on the
  icon). That hide records the time and the cursor (`AppController.trayPopupHidItself`); the next
  click on the icon (`trayPopupShouldReopen`, asked once per tray action) keeps it closed when it
  is the click that took the activation: within 300 ms, or within 3 s when the hide happened with
  the cursor on the icon or within 8 px of the click's (Windows hides it on the press, Trigger
  comes on the release — a held button).

## Folders (the sidebar's tree, "Przenieś do…")

Folders are namespaces of pages (docs/SYNTAX.md §1); moving never breaks a link (§2 "Rewriting on
rename and move").

* **The tree** (`FolderTreeModel` in `ShellSidebar`): lazy (rows only under open folders);
  folders first, then pages, in Polish order with numbers by value (`text::compareNames`, the
  same in All pages and on every platform); the journals folder first, with a calendar icon,
  its days by readable dates ("śr, 14 paź"), newest first, closed by default; attachments hidden
  unless "Pokaż załączniki". Vault changes reach it as inserted and removed rows (never a reset
  while rows stay: the list keeps its place, Syncthing's deliveries included); open folders are
  remembered per notebook (device-local), follow renamed and moved folders and are forgotten
  with deleted ones. A tap opens or closes a folder and opens a page; the open page or daily
  note is revealed (its folders opened, scrolled into view) and highlighted on every route
  change — the "+" menu's "Pokazuj otwartą stronę" (`ui/sidebar/revealCurrent`, on by default)
  turns that off; a collapsed "Strony" stays collapsed (revealed when opened again), only "Pokaż
  w drzewie" opens it. "Strony" counts pages without the daily notes. The revealed row (also a
  new folder's, "Pokaż w drzewie"'s) stays wholly in the list's visible part, above the sync
  footer: `ShellSidebar.revealedPath` is placed again (`placeList()`: the top unless the list
  is kept scrolled away, then the row with the least scrolling) whenever the list's or its
  header's height changes or the sidebar shows, until the user scrolls the list (a flick, the
  wheel or touchpad, the scroll bar, the arrow keys, a row dragged to its edge) or a route shows
  no file. A closed drawer lays itself out only as it opens (Qt's system-bar padding of the
  popup, a keyboard that goes meanwhile, Recent grown): a row placed earlier ended up under the
  footer on Android, where a tap opened the sync center (the shell self-test's `checkFolders`
  makes the window shorter while the drawer is closed to check it;
  `scripts/android-emulator-test.sh --folders` checks it with a real finger). A row revealed
  while it lies wholly in view (a click or a tap on it, Enter, Back) leaves the list where it is
  and keeps it there (`scrolledAway`, as the user's scrolling does): placed from the top, the
  clicked row went to the bottom edge and the next click hit another row. The sidebar shown
  again (the drawer opened, the window's sidebar back) is placed too: a header that grew while
  it was hidden would leave the navigation scrolled out above the list.
* **Menus**: right-click, press-and-hold, the Menu key, and on touch screens a "⋯" button on
  every row (44 px; press-and-hold stays). A folder: Nowa strona tutaj, Nowy folder…, Pokaż
  strony folderu (All pages in it), Szukaj w folderze (search in it), Przenieś do…, Eksportuj
  folder… (the export tab, folder scope), Zmień nazwę…, Przenieś do kosza (the confirmation
  counts its pages and warns for the notebook's special folders). A page: Otwórz w panelu
  bocznym (wide), ulubione, Przenieś do…, Kopiuj link (`PageTools.wikiLink`: the page part of every
  copied link, `notebook::pageLinkTarget` — the shortest target, a daily note by its file name), Zmień nazwę…, Historia strony, Przenieś do kosza. A daily note is a
  file here like any other — renamed, moved and dragged (a misfiled day can be put right) — also
  in All pages, F2 and the Więcej menu of a daily note opened as a page; only the journal view
  (by date) and the page's own menu there offer neither. The "Strony" header's "+": Nowa strona,
  Nowy folder (at the root), Zwiń wszystkie, Pokaż załączniki, Pokazuj otwartą stronę.
* **Drag and drop** (mouse and touchpad only — a finger keeps the list's flick, press-and-hold
  and the drawer's edge swipe): a row dragged onto a folder (a closed one opens after 600 ms; the
  list scrolls near its edges, the target following the rows that pass under the pointer) or a
  page (its folder), or onto the "Strony" header while it shows (the root), is moved there by
  `Workspace.movePath`; the ghost names the target and, a moment later (150 ms on one target),
  what the move rewrites ("Zaktualizowane linki: 3 (w 2 notatkach)", the settings that follow) or
  why it cannot move there — `Workspace.requestMoveImpact()`, planned a few milliseconds per frame
  (`RelocationPlanner`) and dropped when the target changes, so a big folder never stalls the
  drag; impacts are kept per target until the notebook changes. The target is outlined, Escape
  cancels. The toast has Undo (moving it back).
* **"Przenieś do…"** (`ShellMoveDialog`; a bottom sheet on phones): a search over the folders
  (fuzzy: names, then paths), "Główny folder notatnika" first, the current folder shown but
  disabled, a folder's own subtree left out, and "Nowy folder „…”" for the name typed (created,
  then moved into). A chosen folder shows what the move rewrites (`Workspace.moveImpact`:
  "Zaktualizowane linki: 3 (w 2 notatkach)", the special-folder settings that follow, or why it
  cannot move) before "Przenieś tutaj". From the tree, the Więcej menu, the page's menu, All
  pages' rows and the palette. Its pick mode is the new-page dialog's folder picker.
* **Rename** (`ShellRenameDialog`): a "/" moves — a path from the notebook root ("Archiwum/Dom"),
  with a live "Nowe miejsce: …", and its toast names the new name too when the move renames
  ("Przeniesiono do „Archiwum” jako „Dom 2”"); for any name typed it counts exactly what is rewritten
  (`Workspace.renameImpact`: also links a new name would otherwise take over), hidden folder
  names are refused. The page header's title edit renames in place: a "/" there is part of the
  title ("TCP/IP" -> "TCP IP.md"), and its confirmation counts the same way.
* **New page** (Ctrl+N, the top bar, the sidebar's pencil): in the folder of the page shown, as a
  removable chip "W folderze: Projekty ×" (a page at the root: "W głównym folderze notatnika ×")
  with "Zmień…"; without it the notebook's folder for new pages. Pages created from `[[links]]`
  keep `newPagesFolder`. A new folder is shown in the tree.
* **All pages** with a folder: its subfolders as rows above its pages (at the root the top-level
  folders; a tap opens one, back returns), "Z podfolderami" (pages of subfolders too, on by
  default, remembered per device: `allPages/subfolders`), and the top bar's breadcrumb
  ("Wszystkie strony › Projekty › …").
* **Sync**: Nextcloud carries folders faithfully — new empty folders, deletions (only of folders
  that hold nothing unsynced), and moves: a folder or page renamed or moved here is moved on the
  server with one WebDAV MOVE, and one renamed or moved on another device or in the Nextcloud web
  UI is renamed here too (Vault::rename from the sync: the version history, favourites, recent
  pages and folds of its pages follow their `Renamed` changes, open editors follow `pathRenamed`,
  the tree and an open "All pages" of the folder follow `Workspace.folderRenamed`), never a mass
  deletion nor a download plus the trash ("Sync visibility" below). Syncthing syncs folders
  itself.

## On-screen keyboard (`KeyboardInsets`)

One number — how much of the window's bottom edge the on-screen keyboard covers — places
everything near the bottom, the same way on Android, in the browser and with a simulated keyboard:

* **`KeyboardInsets`** (QML singleton; `src/app/ui/keyboardinsets.h`, QtCore only, unit-tested by
  `tst_keyboardinsets`): `overlap` (logical px; 0 when the keyboard is hidden or the system already
  resized the window above it), `shown` (up or coming up), `animating` (sliding: `overlap` follows
  it frame by frame), `slidesOver` (the keyboard slides over the window and `overlap` grows with
  it: Android's report and a simulated keyboard — not the browser, whose page is resized for the
  keyboard, nor Qt's input method), `simulated` (and `simulate(overlap, animating, hiding)`: a
  slide frame by frame, `hiding` one going away — `shown` false from its first frame, as Android
  reports it), `source`. Sources by priority: a simulated keyboard
  (`--keyboard <px>`, the self-tests), the platform's own report — Android's IME insets
  (`android/src/…/ImeInsets.kt`, every frame of the slide on Android 11+, where the window is drawn
  behind the system bars and keeps its size — Qt's expanded client area, set on Android 11–14 by
  `keyboardsources.cpp`, always so on 15+; Android 9–10 resize the window once, no frames;
  docs/ANDROID.md "Klawiatura ekranowa"), the browser's viewports (`setViewportSample`, computed
  by `ViewportKeyboard`: the part of Qt's container the visual viewport leaves out; the page asks
  for `interactive-widget=resizes-content`, so the browser shrinks the page and Qt's window itself
  — overlap 0 — and `shown` comes from that shrink on a touch screen while Qt's keyboard field has
  the focus (held through a focus pass until the readings settle; one tallest page height per
  width); `animating` until the readings settle; docs/WEB.md "Klawiatura ekranowa") — and, as
  the fallback,
  Qt's `QInputMethod::keyboardRectangle` mapped by `keyboardOverlap()` (native pixels on Android,
  the window's place on a split screen, clamped; malformed values give 0). `ui/keyboardsources.cpp`
  connects them. Settled changes are logged (detailed event log): "On-screen keyboard shown,
  overlap 336 px (platform)". `marked` (the browser's `?selftest=keyboard` only) paints the
  keyboard's room magenta and the docked toolbar cyan, so a screenshot shows any gap between them
  (`scripts/android-emulator-test.sh --web-keyboard`).
* **The shell** (`AppShell`) ends its content — the sidebar, the current view and the side panel
  (a page pinned there on a wide touch window, a tablet or a phone held sideways, docks its
  toolbar on the keyboard too) — at the keyboard's top (a strip of `overlap` under it). The
  phone's bottom navigation (`bottomNavShown`) makes way while the keyboard is up
  (`keyboardUp`). When the system hides the keyboard while the editing goes on (Back or the
  keyboard's own key) the navigation comes back and the editing toolbar, docked on the view's
  bottom edge, sits right on it; a tap on one of its tabs navigates as ever (the page view goes,
  the editing with it). A keyboard that slides over the window (`KeyboardInsets.slidesOver`:
  Android 11+, frame by frame) covers the navigation before the shell lets it go — it stays
  under the keyboard until `overlap` reaches its height — and uncovers it going away (`shown`
  is false from the slide's first frame: the navigation is back under it, the content ends at
  the keyboard's top until that passes the navigation's); so a toolbar docked on the navigation
  never drops before it rises with the keyboard, nor jumps when it lands. A sliding keyboard
  that lands below the navigation's top (a suggestion strip over a hardware keyboard) takes its
  place too, but the content keeps the navigation's room (`keyboardFloor`): the toolbar stays
  where it was instead of dropping onto the keyboard after it came, and while it goes. In the
  browser the page is resized for the keyboard in one step: the navigation goes at once and
  comes back at once, fading in (`Theme.durationFast`; none with reduced motion) where the view
  already made room for it, so the view and the toolbar move once. The floating capture button
  makes way while typing — the keyboard is up, or an editing toolbar is docked in the view or
  the side panel —;
  without the bottom navigation (a wide touch window) the content then ends above the system's
  navigation bar. That bar (`AppShell.navigationBar`, which the bottom navigation pads itself
  with) is the window's SafeArea margin — except one taller than any navigation bar (120 px, as
  `ViewportKeyboard::MinKeyboard`): on Android Qt's SafeArea counts the keyboard in at times (336
  px with it up, and for a moment after it went), which would lift the navigation's tabs into
  the middle of the screen; the last margin below that counts then. A view with a bar of its own
  at the bottom instead (`replacesBottomNav`: the drawing editor's tool bar on phones) keeps the
  navigation away. Views tell the shell with
  `toolbarDocked` and `replacesBottomNav` (view contract, `ShellRouteView`). Once the keyboard
  has settled, the detailed event log notes where the view ends ("Typing: the content ends 84 px
  above the window's bottom (keyboard 0 px, navigation bar 24 px, bottom navigation 84 px),
  editing toolbar docked").
* **The touch editing toolbar** (`MobileEditToolbar`) docks on the bottom edge of a dock host that
  never scrolls (`BlockEditor.toolbarHost`): the editor itself on a page, the journal stream's
  frame for its days (a day's toolbar used to scroll with the day and sit at its bottom in the
  middle of the screen). So it sits on the keyboard, follows its slide frame by frame where the
  platform reports the slide (Android 11+; elsewhere — the browser too, where the window itself
  shrinks — it moves once; no animation of its own), and
  stays docked when the system hides the keyboard while editing goes on — on phones right on the
  bottom navigation, which comes back; "Ukryj klawiaturę" ends the editing and takes it away. Its tools follow
  `BlockEditor.toolbarMode`: `block`, `cell` (a table cell — Cofnij, Ponów, Następna komórka,
  Ukryj klawiaturę; `BlockEditor.editingTable`; after Cofnij / Ponów the same cell goes on being
  edited, and every cell tool first commits the word the keyboard is still composing),
  `source` (source mode — `[[`, `#`, `/`, the source's own Cofnij / Ponów, Ukryj klawiaturę),
  `selection` (the touch selection mode: `SelectionToolbar`, "Selecting and copying" — no keyboard,
  the tools at the bottom, on phones right on the bottom navigation).
  Taps beside the buttons stay on the bar. The slot is one Loader per editor (`editToolbarSlot`,
  created on the first use) and each mode's row is made the first time it is used, then kept
  (hidden between edits, where it was scrolled): another tool set docks the same way as another
  mode with its own row, a new button goes into its mode's row.
* **Form fields** (single-line fields and text areas: settings, sync and import forms,
  onboarding, dialogs) have no toolbar; nothing pans the window to them any more (Android:
  `adjustResize`), so `BKeyboardFocusKeeper` (once in `Main.qml`) scrolls the Flickables around
  the focused `BTextField` / `BTextArea` (`keepAboveKeyboard`) until it is above the keyboard —
  when it gets the focus, when the keyboard has settled, when the window or the caret moves.
  Onboarding ends at the keyboard like the shell's content. A word the keyboard is still
  composing (Android's keyboards keep every word as preedit text, outside `text`, until a space)
  counts as typed: dialogs enable their accept button by `typedText` (quick capture's "Dodaj",
  "Utwórz" of a new page or folder, rename), and the word becomes text when the field loses the
  focus (`EditorInput.commitPreedit`) or the dialog is accepted (`BDialog.tryAccept`) — a single
  word typed into an empty field used to leave the button disabled.
* **What is being edited stays above the toolbar**: the caret or the table cell
  (`BlockEditor.ensureEditVisible()`, the journal's `ensureVisible()` with the toolbar's height;
  a day's editor asks the journal list, `BlockEditor.scroller`), checked again when the view's
  height changes — not while the keyboard slides, once it has settled; the source's caret
  likewise.
* **Popups**: bottom sheets (`BDialog`) sit on the keyboard and centred dialogs stay above it;
  menus (`BMenu.bottomMargin`) stay above it, the editor's task status menu also above the
  toolbar, and below the status bar of an edge-to-edge window (`BMenu.topMargin`, the window's
  safe area: on Android a finger's tap there reaches the status bar, so a block menu opened
  above the toolbar used to leave its first items untappable; one taller than the room left
  scrolls). A menu opened right above the toolbar (its Więcej, its task button held down) takes
  the keys, so the system hides the keyboard and the toolbar drops onto the bottom navigation
  (to the bottom on a wide window): the menu
  follows it (`BlockEditor.followToolbarWith`), with the room the keyboard left — every item
  shown, "Usuń blok" no longer scrolled away — until something in it is pressed or scrolled
  (`BMenu.touched`): it never moves away from a finger aiming at it. Autocomplete is placed above
  the toolbar (`BlockEditor.visibleBottom()`); toasts above the keyboard, the bottom navigation
  and the toolbar (`AppShell.toastClearance`), except while text is typed — the keyboard up, or a toolbar
  docked while a view edits text (`AppShell.toastsAtTop`): then they show at the window's top,
  one at a time (`BToastHost.atTop`), since the caret's line is kept right above the keyboard and
  its toolbar, where they covered it ("Scalono…" while typing on a phone); the command palette
  ends at it.
* **Tests**: `--keyboard <px>` simulates a keyboard, drawn as a grey `BKeyboardPlaceholder`:
  `tst_editor_qml_keyboard_touch` (the playground's `keyboard` script: a block in an outline and
  a document, a slide frame by frame, the system hiding the keyboard, typing, autocomplete,
  source mode and a tap beside the bar's buttons, a table cell — a word being composed, undo and
  redo in the cell —, the tool rows kept between edits), the journal stream's self-test (`tst_content_journals_qml`: the
  toolbar on the keyboard, not at the day's bottom, while the list scrolls or is flicked),
  `tst_shell_qml` (the bottom navigation, the view's end, a sheet, a toast; editing with the
  keyboard hidden: the bottom navigation shown and the docked toolbar right on it, also over a
  system navigation bar — the shell's `SafeArea.additionalMargins` —, the capture button away;
  the keyboard sliding in frame by frame leaving the navigation under it until covered, sliding
  away uncovering it, a keyboard shorter than the navigation leaving the toolbar where it was,
  a browser's keyboard — viewport samples, the page resized — taking it at
  once; a tap on Kalendarz while editing ending the editing; a toast while typing at the top,
  away from the caret's line, a settings field scrolled above the keyboard, a page in the side
  panel of a wide touch window; the drawing editor's tool bar keeping the navigation away),
  `tst_keyboardinsets` (`slidesOver` per source, a simulated slide down),
  `tst_editor_qml_blocklinks*` (the block menu from the
  toolbar's Więcej following the toolbar down when the keyboard goes, every item shown, and
  staying put once touched),
  `tst_appio_import_qml` (onboarding's folder field); screenshots
  `page-editing-keyboard`, `journals-editing-keyboard`, `capture-keyboard` (and `page-editing`:
  the keyboard hidden, the toolbar on the bottom navigation); on Android
  `scripts/android-emulator-test.sh --keyboard` (Back: the navigation's tabs below the toolbar,
  a tap on the text: the keyboard back, the toolbar on it, the tabs gone; and `--folders`: the
  toolbar's "Więcej" and the block menu with a real finger), in the browser
  `scripts/wasm-smoke.mjs --keyboard` and `--keyboard-phone` (the keyboard hidden: the toolbar
  on the bottom navigation, `bottomNavTop` in the self-test's report) and
  `scripts/android-emulator-test.sh --web-keyboard`.

## Keyboard shortcuts (desktop)

| Keys | Action |
|---|---|
| Ctrl+K | command palette |
| Ctrl+O | quick switcher (go to a page) |
| Ctrl+Shift+F | search |
| Ctrl+J | today's journal |
| Ctrl+N | new page |
| Ctrl+Shift+N | quick capture (from the documentation notebook: back to the user's notebook first) |
| Ctrl+Shift+B | the documentation notebook / back to the notebook used before |
| Alt+← / Alt+→ | back / forward |
| Ctrl+\\ | side panel on/off (wide windows) |
| F2 | rename the current page |
| Ctrl+, | settings |
| Ctrl+E | Markdown source mode of the page / journal |
| Ctrl+; | add a page property (the properties panel's add row; "Page properties") |
| Ctrl+Enter | (in dialogs) accept |

Block editor (handled in `qml/editor/BlockEditor.qml`, executed by `PageController`):

| Keys | Editing a block | Blocks selected (Esc, Shift+↑/↓, Ctrl+A, a drag, …) |
|---|---|---|
| Enter | split the block at the cursor (task checkboxes continue); at the start of the text: an empty block above it, the caret stays in the text (a task: an empty task above; the text keeps its status and children — the hidden marker does not count, `BlockDocument::split`); at the end or the start of a zoomed list item: a new first child inside the zoom, in its middle its tail becomes the first child (also without children or folded: a sibling would be outside the zoom); a zoomed heading or paragraph has no children (only list items nest): at its start it splits as usual (the empty paragraph above stays out of sight, the caret stays in it), anywhere else the zoom ends and Enter works as on the page — the new block below is edited and kept in view (`BlockEditor` `onZoomChanged`), Undo zooms back in and Redo ends the zoom again — the caret never lands in a hidden block; a new line in multi-line blocks (code, calc, query, quote, callout) | edit the block (the focus end) |
| Shift+Enter | soft line break | — |
| Shift+↑ / Shift+↓ | select text; on the first / last line once the text selection reaches the block's start / end: select this block and the previous / next one | extend or shrink the selection (the end moves over whole subtrees) |
| Ctrl+A | select the block's text; again (all of it selected, or an empty block): every block | every block (in a zoom the zoomed subtree) |
| Ctrl+C / Ctrl+X | copy / cut the selected text exactly (no-break spaces kept) | copy / cut the blocks (Markdown with HTML; "Copying" below) |
| Ctrl+V | a copied picture or files become attachments `![[…]]`; text: Markdown splits into blocks, HTML from other apps comes as Markdown | the clipboard replaces the selected blocks |
| Backspace at the start | remove the task marker, else merge with the previous block | delete the blocks |
| Delete at the end | merge the next block into this one | delete the blocks |
| Tab / Shift+Tab | indent / outdent with children (Tab in a multi-line block: a tab character) | indent / outdent (a range all or nothing) |
| Alt+↑ / Alt+↓ | move the block with its children | move (a run of sibling blocks) |
| ↑ / ↓ | on the first / last line: to the previous / next block | select the previous / next block alone |
| Ctrl+Enter | cycle the task status (none → todo → doing → done → none) | same (every block gets the next status of the first one) |
| Ctrl+Shift+Enter | the task status menu at the caret (any status, or none; list items and paragraphs only) | the same menu under the block |
| Ctrl+. | fold / unfold | same |
| Ctrl+Shift+C | copy the block reference `((id))` (an id is added when missing; the browser may keep the keys for its own tools: the palette's "Kopiuj odwołanie do bloku" then) | same (the focus end); nothing selected: a toast says what to do |
| Ctrl+Z, Ctrl+Shift+Z / Ctrl+Y | undo, redo (per page) | same (undoing a deletion selects the blocks again) |
| Esc | close the suggestions, else stop editing and select the block | deselect (also Android's Back in the touch selection mode) |

## Navigation

`AppController` keeps the main view's route history (`navigation.h`: `RouteHistory`, routes are
`QVariantMap`s `{view, path?, line?, blockId?, date?, tag?, query?, section?, …}`);
`ShellRouteView` shows the view for the current route (and for the side panel's route).

| Route `view` | View | Parameters |
|---|---|---|
| `journals` (home) | `JournalsView` — the stream of daily notes, "Przejdź do dnia…", missed reminders banner (`ContentMissedBanner`: Otwórz, Drzemka, Zrobione — a task's reminder only: `reminders.complete(id)` marks the task done in the note like the status menu, the shell's toast "„…” — zrobione" confirms it; no undo step in an open editor, as with the snooze — and dismiss) | `date?` |
| `journal` | `JournalsView`, one day (opened "jako strona") | `date`, `path?` |
| `page` | `PageView` — page header (tags and aliases as chips, edited there: "Page properties"; note mode toggle, favourite, page menu with "Dodaj właściwość"; for a page named by a tag in use the tag strip: "Widok tagu", "Zadania z tym tagiem", its sub-tags as chips — with a rename at once), the block editor (outline or document), linked references (links and the blocks / pages tagged with a tag naming the page — "cała strona" for a page tagged as a whole —; a page not written yet lists what already names it), unlinked mentions. A page not written yet opens ready to type, except one opened from a `#tag` (`fromTag`, set by `Workspace::resolveRoute`) on a touch UI: the keyboard would cover the tagged blocks listed below, so the header's "Zacznij pisać" or a tap under it starts typing. `line` / `blockId` / `heading` scroll to that block, select it and flash it (a heading by `PageController.rowOfHeading`, docs/SYNTAX.md §2; one that is not there: a toast, the page at its top); `Workspace::resolveRoute` sends a `blockId` the page no longer has to the page holding it | `path`, `line?`, `blockId?`, `heading?`, `open?`, `fromTag?` |
| `calendar` | `CalendarView` — Miesiąc / Tydzień / Agenda, the day panel, add dialog | `date?`, `mode?` |
| `tasks` | `TasksView` — quick add, filters, sections (Zaległe, Dziś, Jutro, W tym tygodniu, Później, Bez daty, Zrobione), "Pokaż jako zapytanie"; a click on a task's `#tag` filters the list, Ctrl+click opens the tag's page, a long press or right-click its menu (Filtruj po tagu, Otwórz stronę tagu, Widok tagu, Otwórz w panelu bocznym, Kopiuj tag); a right-click or long press on a chip: its token's menu ("Token and link menus") | `query?` |
| `graph` | `GraphView` — whole / local graph (depth 1–2), toggles for tags (the "#tag" nodes of tags that name no page; a tag naming a page is an edge to it), missing pages, journals, templates. The layout is computed on a worker thread (in slices between frames in the browser) and unfolds over about a second at the display's frame rate (phones 60 fps): pan, zoom, select and drag work from the first frame, a dragged node follows the finger at once. Reduced motion: the layout is computed without animation and shown once; a fresh one shows a spinner (`graphModel.settling`) instead of a spiral that jumps — also when a filter, the local graph or the index changes before it is shown. Drawn through the scene graph (GPU) | `path?`, `depth?`, `select?` |
| `search` | `SearchView` — results in a folder of any depth with `folder` (the tree's "Szukaj w folderze"; the route follows its renames and moves) | `query?`, `folder?` |
| `allPages` | `AllPagesView` — with `path` a folder browser: its subfolders as rows, its pages (with subfolders' or not), a breadcrumb in the top bar; follows the folder's renames and moves | `path?` (folder), `sort?` |
| `tags` / `tag` | `TagsView` — every tag (a row opens the tag's page, its trailing button the tag overview) / the tag overview (the pages and blocks carrying the tag, parent and sub-tags, "Otwórz stronę" or "Utwórz stronę", "Zadania z tym tagiem"). A tag names a page (docs/SYNTAX.md §3): a click on `#tag` anywhere asks `Workspace::navigate({view: "tag", tag})`, which `resolveRoute()` turns into the page route (`Workspace.tagPage`); routes the shell navigates to itself (`AppController.navigate`, `--route`) and `overview: true` keep the overview | `tag?`, `overview?` |
| `sync` | `SyncView` — Przegląd, Syncthing, Nextcloud (live progress, history of runs), Konflikty (merge; drawings element by element) | `tab?`, `copy?`, `run?` (open a run's details) |
| `importExport` | `ImportExportView` — import: the source apps as tiles (`ImportSourcePicker`), the chosen one's export steps (and the phone's), pick files / a folder (the system's picker, `FilePicker`; the browser's own, several files at once) / a typed path, the preview, the report. The Microsoft To Do card adds `MsTodoSignIn` above the file buttons: the Application (client) ID field, the authority under "Więcej" (both remembered on the device), **Zaloguj się w Microsoft**; then a well with the device code in large monospace, **Kopiuj kod**, the address as text and an **Otwórz microsoft.com/devicelogin** button, a spinner with the progress, the minutes left and **Anuluj**; an error box explains AADSTS failures; the downloaded lists go into the usual preview. In the browser only a hint (the file from Graph Explorer). Export (`ExportPanel`): the format tiles, what to export (notebook / folder / page), for the HTML and Pandoc formats **Obrazy z internetu**: Zostaw adresy / Dołącz kopie (`ExportController.webPictures`, remembered on the device: `AppSettings.exportWebPictures`; with copies the progress says "Pobieranie obrazów z internetu (1 z 3)…" and a picture that could not be fetched is a warning of the report; pictures of this device or its private network are included only from the cache — never fetched by the export —, and past 256 MB of copies the rest keep their addresses, each case one warning), Eksportuj…, the progress card, save (the system's save picker, `FilePicker`; the browser's save picker or a download) | `tab?`, `importer?`, `files?`, … |
| `history` | `HistoryView` — versions of a page, diff, restore; each says what replaced it (your edit, another program, sync, an import, the command line — "Zastąpiona z wiersza poleceń (agent)" with a terminal icon —, a restored version) | `path` |
| `trash` | `TrashView` | — |
| `settings` | `SettingsView` — Ogólne, Wygląd (`appearance`), Edytor, Notatnik, Przypomnienia, Synchronizacja, Aplikacja (on the desktop "Działaj dalej w zasobniku systemowym" and "Kliknięcie ikony w zasobniku" Okno | Kalendarz — `AppSettings.trayClick`, the tray calendar above, disabled without a tray; on Windows "Uruchamiaj po zalogowaniu" — `AppController.startAtLogin`, the Run key, shown disabled with a separate `BRULION_PROFILE`, docs/WINDOWS.md "Autostart"; on the desktop with "Dla programistów": the switch that lets AI agents drive the running app, docs/CLI.md "Remote control"). Przypomnienia → sounds: on Windows the "Dzwonek" menu lists Windows' notification sounds (Powiadomienie, Alarm 1–10, Połączenie 1–10…) and "Plik dźwiękowy…" is disabled | `section?` |
| `logs` | `LogsView` — "Rejestr zdarzeń", the event log (see below) | `group?`, `level?`, `fromMs?`, `toMs?`, `run?` |
| `drawing` | `DrawingView` (`qml/drawing`) — the drawing editor on a drawing file ("Drawing editor" below); title: the file's name without `.svg`, `.excalidraw`, `.excalidraw.md` | `path`, `open?` (screenshots: `style`, `more`, `paper`, `theme`, `shape`, `select`, `backdrop` — the page picker of the backdrop's PDF) |

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
  rendered rich text (links: `page:`, `tag:`, `block:`, `ext:`, `task:`, `reminder:` URLs; the
  token menus' `date:`, `recur:`, `prio:` and `day:`).
  Clicking or tapping it edits it inline (`InlineEditor`: a `TextEdit` with the raw Markdown,
  minus the task marker / fences the delegate draws, and `MarkdownHighlighter`).
* `PageController` executes commands (one undo step each, typing coalesced), saves debounced
  and on focus loss / suspend / quit, and merges external changes three-way. An undo step is a
  `BlockDocument::Delta` — the run of blocks the command changed, a block in both versions with
  only its changed range of text (`undoStoresChangesNotPages`, `pasteIsASmallUndoStep`) — and
  undo / redo apply it as a document operation through `BlockListModel::apply()`
  (`PageController::applyStep()`: `BlockDocument::revert` / `reapply`): nothing is read again,
  only the changed blocks are parsed, every block keeps its uid. Rows of unchanged blocks stay,
  a changed block keeps its row (`dataChanged`: its delegate, fold and a tracked handle survive —
  `undoKeepsChangedRowsInPlace`; an inline editor open on it gets the step's text; a table cell
  being edited is closed before the step — its field would keep the old text and write it back —
  and the cell toolbar's Cofnij / Ponów open it again), only blocks the step adds or removes
  come and go, never a reset; undo costs about what the command cost (`largePageTouchesOneRow`
  compares their medians). Typing merges into one step per block (`BlockDocument::composed`); a
  keystroke that also changed blocks far away — typing at the top of a page renumbers a loose
  ordered list further down — stays a step of its own (`typingAfterAFarChangeIsAStepOfItsOwn`). The caret goes back to the block and the place it had — also when the
  undone step put a block at the very start of the page: Enter at the start of the first block, a
  pasted list, Alt+Up, below a hidden note-mode frontmatter (`tst_editor_controller`
  `undoOfAnInsertionAtTheStart` follows the rows as a view sees them from the model's signals).
  `zoomChanged` and `foldStateChanged` follow a step only when the zoom (its breadcrumb) or the
  lines of folded blocks changed. The zoom is view state: undo / redo leave it as it is (it
  follows its block; a step that removes the zoomed block ends it), except for Enter made in a
  zoom — it records the zoomed row of both versions (`PageController::Focus::zoom`), so undo zooms
  back in (also after Enter in a zoomed heading or paragraph ended the zoom, or split the zoomed
  block's text) and redo restores the zoom after it (`zoomRootTextBlockEnterLeavesZoom`,
  `zoomRootListItemEnterInTheMiddle`).
  Undo / redo never leave the caret or a block selection in a hidden block: a zoom that does not
  hold the restored block ends — one chosen after the step, or the zoom an Enter next to it
  brought back — and the folds over it open, as when a hidden block is edited (`applyStep()` →
  `reveal()`; `zoomUndoRedoNeverHideTheCaret`, `zoomUndoRedoNeverHideTheSelection`). Zooming out
  (`zoomTo()`: the breadcrumb, its arrow) keeps the edited block shown too: the zoom root's
  children are shown whatever its fold, so leaving a folded zoom root opens the folds over the
  edited block; a zoom that does not hold it stops editing, as `zoomInto()` does
  (`zoomOutKeepsTheEditedBlockShown`).
  The `line` role (the block's first line in the file) is announced with `dataChanged` for the
  rows a change above them moved. Empty paragraphs (Enter at the start of a paragraph or
  heading, a paragraph typed empty, a document's last empty paragraph) are only blank lines in
  the file, so a re-read text has none; undo / redo bring them back as the blocks they were (with
  their uids; the text unchanged byte for byte — `tst_editor_controller`
  `undoKeepsEmptyParagraphs`). A reload from the file (or an external change) still reads them as
  blank lines. Whole-text changes — another device's version, Markdown pasted
  (`insertMarkdown`), a template, the note mode, source mode, a resolved conflict — go through
  `BlockListModel::replaceContent()`, which matches the blocks by kind, text and depth and gives
  the kept ones their old uids (`BlockDocument::adoptUids`): their rows, folds, zoom and editing
  stay, and the undo step of such a change holds only the changed blocks
  (`externalReloadKeepsUidsOfKeptBlocks`).
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
  it a code block. Such a line is saved closed while it is typed when it would take the blocks
  after it into its code (any block after a paragraph, a list item's children) — and so it is
  compared with the file, merged with another device's change and shown in the merge dialog —,
  and leaving the block (another block gets the caret, editing stops) closes it — an empty code
  block, one undo step; a lone fence line that came with the file and was only looked at stays
  as it is. The two lines then act as a pair: a longer opening fence lengthens the closing
  one, Backspace over the opening fence (or deleting its line) removes the closing one, and a
  deleted or changed closing fence is added again at the end of the block. Between them Enter and
  paste work as in a code block: a line break, the pasted text as code (a split would leave half
  a fence in each block). A fence-like line inside a display formula between `$$` lines is part
  of the formula, as in the parser: it opens nothing and gets no closing fence.
* **Autocomplete popup** while typing (`SuggestionModel`, `SuggestionPopup`): `[[` pages
  (+ "Create page “…”" as the first row whenever no page or alias has the typed name — folded
  for case, diacritics and runs of spaces — or folder path, and `[[typed]]` resolves to nothing; on top of the `limit` matches, so a long list never hides it; the
  highlighted row Enter/Tab take is still the first existing match, `SuggestionModel.defaultIndex`,
  and Up reaches the create row), `((` blocks (creates the target's block id), `#` tags and pages (a tag names a
  page: tags in use first, with the page they open, then pages no tag names yet with their
  folder — a duplicate base name by the shortest path that names it, `VaultIndex::suggestTags` /
  `tagTarget` —; a name that is no simple tag is written
  `#[[Jan Kowalski]]`, `#[[C++]]`), `/` slash commands,
  natural-language dates after `📅 ⏳ 🛫 ⏰ ⏰!` or `@` (Polish and English, e.g. "jutro o 9";
  `@` also offers the journal link and a reminder). After a written date or reminder (`⏰
  2026-10-15 09:00`, `(@2026-10-15 09:00)`) the rest of the line completes as anywhere else
  (`/`, `#`). A reminder added from the dialog where the caret was at the end of the text leaves
  the caret after it and a space, as a completed date does, so a `/` typed next opens the menu.
  Such a space the editor adds after a completed date or tag or a reminder at the end of the
  block is not saved while nothing follows it, and goes when the caret leaves the block (part
  of the undo step that wrote it; `PageController::rememberEditorSpace`).
* Slash commands (`/`, Polish and English names, diacritic-insensitive filter): the task
  statuses Zadanie: do zrobienia / w toku / zrobione / oczekujące / anulowane / bez statusu
  ("Task: to do" … "Task: no status"; also found as todo, doing, done, czeka, odłożone, waiting,
  anulowane, cancel, bez statusu…), Przypomnienie…, Alarm…, Termin…, Zaplanuj…, Powtarzaj…, Dziś,
  Jutro, Data…, Tabela, Obliczenia, Zapytanie, Nagłówek 1–3, Cytat, Callout, Kod, Wzór
  matematyczny, Szablon…, Obraz…, Rysunek (a new drawing, opened in the drawing editor), Rysunek na
  obrazie lub PDF… (drawings on a picture or a PDF's pages: "Drawing editor" → Backdrop), Osadź,
  Odwołanie do bloku, Priorytet wysoki / niski, Linia
  pozioma, Aktualna godzina. Under each name: the syntax it writes ("⏰ data godzina", "- [ ]
  zadanie", "blok ```calc"). A status command sets the status of the block being edited
  (`PageController.applySlashCommand` → `setTaskStatus`) in the same undo step that removes the
  "/command": a block that is no task becomes one (a paragraph a list item), Logseq keywords keep
  their workflow (TODO → DOING, LATER → NOW), "bez statusu" removes the checkbox and its ✅ / ❌
  date, and the caret stays in the words around the command.
* **Block menu** (`BlockMenu`, `qml/editor`; one per editor, made when first opened, the block
  tracked while it is open): **Kopiuj**, **Wytnij**, **Kopiuj jako tekst** (the block with its
  children, which becomes the selection — or, for a block that is part of a selection of several,
  "Kopiuj 3 bloki", "Wytnij 3 bloki": the whole selection; indenting, moving and "Usuń 3 bloki"
  then act on all of it too — a selection with gaps is not indented or moved: those items are
  shown disabled, a tap says why), **Zaznacz** (the block becomes the selection; on touch UIs the
  selection mode), Przybliż and Zwiń / Rozwiń (outline), **Kopiuj odnośnik** ▸ —
  Odwołanie do bloku `((…))` (Ctrl+Shift+C), Link do bloku `[[…#^…]]`, on headings Link do
  nagłówka `[[…#…]]`, Osadzenie (`!((…))`, a heading's section `![[…#…]]`); what each writes:
  docs/SYNTAX.md §2 "Headings and blocks in links" —, Wstaw obraz…, Wstaw rysunek, Zwiększ / Zmniejsz wcięcie
  (outline, list items in a document), Przenieś w górę / w dół, Usuń blok. It opens for every
  block in both note modes: a right-click anywhere on a block — text, heading, a code, calc, table,
  query or callout frame, an embed — with a mouse, touchpad or stylus, never a finger (one
  `TapHandler` on the editor's list, not one per block, picking the block drawn under the pointer
  — `BlockEditor.rowAt`, also the page title drawn above its properties —; the page properties
  have none), the bullet or an ordered item's number (a right-click — their press is theirs, the
  list's handler does not open it twice — or a long press), and on touch UIs the editing
  toolbar's **Więcej** (the menu right above the bar; in a table cell the cell tools' Więcej ends
  the cell's editing, selects the table and opens its menu at it). A right-click on a token (a date,
  a reminder, 🔁, a priority, a link to a day) or a link opens its own menu instead (below, "Token
  and link menus"). A long press on text opens no block menu: it selects the block (on a token or
  a link: its menu) (the touch selection mode, "Selecting and copying"). The submenu opens beside the menu with a mouse, in place of it on touch UIs (`BMenu`:
  `cascade` off). Every copy says "Skopiowano …", or why nothing was copied — no clipboard (the
  text read back differs: never "copied" then), a block that cannot carry an id (the page
  properties), Markdown source mode, the documentation notebook (no new block ids there:
  `PageController.allowNewBlockIds`, so its pages keep being updated).
* **Following a link** to a block or a heading (`((id))`, `[[Strona#^id]]`, `[[Strona#Nagłówek]]`,
  Obsidian heading paths, also on the page already shown and on every click —
  `PageController.activateLink` scrolls there itself instead of asking for an unchanged route):
  `BlockEditor.revealRow` unfolds and leaves a zoom as needed, scrolls the block to the middle (a
  day of the journal stream: the stream scrolls it into view — `scroller.ensureVisible`, the day's
  own list never scrolls), selects it and flashes it (`BlockDelegate.flash()`: `Theme.mark` over the selection and under
  the text, ~1.2 s, created only while it shows, no height change); a heading or block that is
  nowhere is said ("Nagłówka „…” nie ma na tej stronie"; also from a query result). Toasts show
  their message as plain text (`BToast`): they quote note text — a copied link, a heading —, which
  must never be rendered as markup.
* **Task status menu** (`TaskStatusMenu`, `qml/editor`): Do zrobienia, W toku, Zrobione,
  Oczekujące, Anulowane, Bez statusu, the current one checked. It opens with a right-click (mouse,
  touchpad, stylus) or a long press on any task checkbox (`TaskBox`) — in the editor, query
  results, embedded pages and blocks, linked references and tag pages, the calendar's day panel
  and agenda, Tasks (whose row menu adds "Otwórz") —, with Ctrl+Shift+Enter in the editor and by
  holding the touch toolbar's Zadanie button. A click or tap on the checkbox still toggles (a long
  press never does). The editor's menu (`BlockEditor.openTaskStatusMenu`) follows its block while
  open and gives the keyboard back to the edited block; elsewhere the choice goes through the
  view's model to the task's own file (`QueryResultModel.setTaskStatus`,
  `PageController.setTaskStatusAt`, `GroupedRefsModel.setTaskStatus`,
  `PlanningActions.setTaskStatus`; "" = no status, `markdown::removeTaskMarker`); Tasks and the
  calendar show "Zmieniono status" with Undo (the dates, reminders, rules and priorities of those
  rows have menus of their own: "Token and link menus"). While the menu is open on a query result or an
  embedded task, their rows wait: results that change meanwhile (a sync, an edit in another note,
  the index catching up) would replace the rows and take the menu away with its checkbox. A
  `TaskBox` says so with `menuOpen`; the query block holds its `QueryResultModel`
  (`hold(box)` / `release(box)`, `held`: runs due meanwhile wait and run once, debounced, when the
  last holder lets go — a holder destroyed with its menu open lets go by itself), the embeds
  (`TransclusionView`) show the embeds from when the menu opened (`shown`) and follow
  `transclusions` again a moment after it closed. A status chosen on rows that waited goes to the
  same block where it is now (the query run again / the current embeds: same file and text, the
  nearest line), never to whatever took its old line; a task that is gone is left alone. A
  result matches by the item's `text` (no status marker, no date tokens: the menu's own choice or
  another device may have changed them meanwhile; the unchanged `raw` only breaks a tie between
  two as near), and after a write the row keeps the line it wrote to. A task that the query no
  longer lists (its new status left a `status:` filter) is not found: a further click on it
  before the rows catch up does nothing. Tasks
  (`TaskListModel`) update their rows in place (inserts, removals, moves by `path:line`), so a
  row's menu stays open unless its own task moves to another line.
* A day without a note (journal stream or a single day) starts from the journal template; the
  note is created by the first change (`PageController.startNewPage`), never by looking at it.
* Special block delegates: code (monospace), `calc` (input lines with right-aligned results),
  table (grid editor with formula results; edits rewrite the Markdown table), `query` (live
  results with checkable tasks), math (`math`/`latex`/`tex` fences, the formula rendered from
  the LaTeX source, also as a list item of an outline or a journal; a formula between two `$$`
  lines stays one text block whatever its lines start with; a block an edit leaves with a `$$`
  line and no closing one — a deleted `$$`, Enter inside the formula — gets a blank line after
  it when a later `$$` line would close it, so the items between never become formula text when
  the page is read again), callout, embeds/transclusions
  (`![[…]]`, `!((…))`), images, headings, horizontal rule, properties / frontmatter
  ("Właściwości" panel: typed fields, "Page properties" below).
* Chips inside rendered text: 📅 due (red when overdue), ⏳ scheduled, ⏰ reminders (amber,
  bell for alarms), 🔁 recurrence in Polish words. Clicking a ⏰ chip opens the reminder dialog
  (edit / snooze / delete); a click on another date chip, a 🔁 chip or a priority edits the text
  (an I-beam cursor). "dziś"/"jutro" and overdue colours of open pages move on at midnight
  (`DayClock`). Links to daily notes read as dates (`[[2026-10-13]]` → "wt 13 paź").
* **Token and link menus.** A right-click (mouse, touchpad, stylus) or a long press (a finger,
  outside the selection mode) on a token or a link of rendered text opens its menu instead of the
  block menu (BlockText's own handlers: the token's menu, else the link's, else the block menu as
  the list's handler opens it, `BlockEditor.openBlockMenuAt`; a long press elsewhere selects the
  block). `DateChipMenu` (`qml/editor`) by the token's kind:
  * 📅 ⏳ 🛫 ➕ ✅ ❌: Zmień datę… (the date dialog on that token, `EditorDialogs.pickDateAt`), Dziś,
    Jutro, Dzień później, Tydzień później, Usuń datę;
  * ⏰ / 🔔: Zmień datę i godzinę… (the reminder dialog), Godzinę później, Jutro o tej porze,
    Tydzień później, Alarm (checkable), the submenus **Powtarzanie** (Zmień regułę…, the quick rules
    of the recurrence dialog — Codziennie, W dni robocze, Co tydzień, Co 2 tygodnie, Co miesiąc,
    Co roku —, Nie powtarzaj: the block's 🔁, which makes the reminder recur, SYNTAX §6) and, on a
    task, **Priorytet** (Najwyższy … Najniższy, Bez priorytetu: the task's priority, added to its
    first line when it has none), Usuń przypomnienie;
  * 🔁: Zmień regułę… (the recurrence dialog on that rule, `EditorDialogs.pickRecurrenceAt`), the
    quick rules (the current one checked), Od daty wykonania (`when done`, checkable), Nie powtarzaj
    — the rule is written canonical, text after it ("🔁 co tydzień kupić mleko") stays;
  * a priority (🔺 ⏫ 🔼 🔽 ⏬; Logseq `[#A]` `[#B]` `[#C]` with three levels, written as Logseq):
    Najwyższy … Najniższy, the current one checked, Bez priorytetu;
  * a link to a day of the journal (`[[2026-10-20]]`, also `[[20.10.2026|wtorek]]`): Otwórz dzień,
    Otwórz w panelu bocznym (not on narrow windows), Kopiuj link, Zmień datę… (a date without a
    time), Dziś, Jutro, Dzień później, Tydzień później — only the date part of the target changes,
    in the format it was written in (an alias, a folder, ".md" stay).

  `LinkMenu` (`qml/editor`) on a `[[page]]` (also a Markdown link to a note): Otwórz ("Utwórz
  stronę" for one not written yet — created by its first content, as a click does), Otwórz w
  panelu bocznym, Kopiuj link (`[[Strona#Nagłówek]]` by the page's shortest link target — it works
  pasted on any page —, as written for a page that does not exist; `PageController.linkMenu`, in Tasks and the calendar
  `PlanningActions.linkText` / `linkMissing`); on a
  `#tag`: Otwórz stronę tagu, Otwórz w panelu bocznym, Widok tagu, Zadania z tym tagiem, Kopiuj tag
  (`#tag`, `#[[tag ze spacjami]]`); on a `((block))`: Otwórz, Otwórz w panelu bocznym, Kopiuj link;
  on a web address: Otwórz w przeglądarce, Kopiuj adres. Every copy says "Skopiowano …" or that the
  clipboard is not available (WebAssembly may refuse).

  Where they open: the editor's blocks (`PageController.dateChip`, `applyTokenAction`: one undo
  step each; the block is tracked while the menu is open), **table cells** (ranges in the cell's
  text, `PageController.tableCellChip` / `applyTableCellToken`: only the cell's text changes in its
  row as written — the other cells, the padding and the pipes stay —, one undo step; a `[[day]]` in
  a cell is a page link with the link menu), **query results** and **embeds** (the item's block as
  Markdown — the `raw` role, the embeds' `raw` — `PageController.tokenChipIn`, `editTokenIn`: the
  block with that text on the item's line, else the nearest one with it — the index may be a moment
  behind, the line may hold another block with an equal date by now —, none: nothing is written;
  on this page an undo step of the editor, in another file a hand edit written only when the file did not change
  meanwhile — `EditorHost::writeIfUnchanged` — with a toast "Zmieniono datę" / "Zmieniono
  powtarzanie" / "Zmieniono priorytet" and **Cofnij**, `undoTokenEdit`, which puts the file back
  only while it still holds that edit; a token that is not there any more: "Notatka zmieniła się w
  międzyczasie"), **Tasks** (the chips of a row: due, scheduled, start, the reminder, the rule, the
  priority) and **the calendar** (an entry of the day panel, the agenda, the overdue list, the
  week grid) — `PlanningTokenMenu` (`qml/planning`) with `PlanningActions.editToken` (the task by
  its text near the index's line, like the status menus) and the views' "Cofnij" toasts
  (`PlanningActions.undo`); `PlanningActions.tokenMenu` reads the block's rule and priority from
  the file (the rule at the start of a 🔁 token with words after it, a Logseq `[#A]`) and opens no
  menu for a value that is no token there (a Logseq `SCHEDULED:` / `DEADLINE:` line, a `.+1w`
  repeater). A calendar entry that is another date of a recurring series (`CalendarModel` entry
  `occurrence`; `spec` is the token as written) offers "Zmień całą serię…" instead of moving one
  date, and a reminder's time that has rung offers Godzinę później / Jutro o tej porze, a snooze of
  that one time (`PlanningActions.snoozeReminder`: a one-off child reminder, as the notification's
  "Drzemka"); a time still to come offers no snooze (it would ring as well). The 🔁 rule changes only
  in its own text (`markdown::setBlockRecurrence`: words after it stay; the same rule in another
  phrasing — the reminder dialog with only the time changed — stays as written). The tokens are named across the copies of a block by
  `markdown::TokenRef` (kind, value, which of the equal ones; markdown/tokenedit.h); the edit is
  `markdown::editBlockToken` — only the token's bytes of its line change (CRLF, indentation and
  every other line stay). The rendered text carries the links (`RenderContext::dateLinks`:
  `date:FROM-TO`, `recur:FROM-TO` — the range ends with the rule —, `prio:FROM-TO`; `dayLinks`, the
  editor's own blocks only: `day:FROM-TO` for links to days, opened as their `page:` link; ranges
  in the block's body, `reminder:` always). Closed tasks keep their menus (a finished task's dates
  can be corrected).
* Source mode (Ctrl+E, Więcej → Źródło Markdown): one text area with the whole file and
  highlighting — also where any characters across several blocks are selected (the block editor
  selects whole blocks: "Selecting and copying"). A mouse selects by dragging; on touch UIs a
  finger scrolls it (a TextEdit that selects by dragging keeps the finger: the source could not be
  scrolled at all), a tap places
  the cursor and a long press selects the word under the finger (a TapHandler on an item above
  the TextEdit, which leaves a finger's taps alone too when it does not select by dragging; the
  system's selection handles take a selection on where the platform has them).
* Drawings (an SVG with an embedded scene, a `.excalidraw` scene, a drawing of Obsidian's Excalidraw
  plugin — `![[Szkic.excalidraw]]`, `![[Szkic.excalidraw.md]]`, a page embed `![[Plan]]` of a
  plugin file, not a transclusion then —, docs/SYNTAX.md §2 "Drawings"; the embeds' `drawing` and
  `path`, `EditorHost::isDrawing`, the size of a scene's preview `EditorHost::drawingSize`): shown
  at their own size (one scene unit a pixel, never enlarged as other SVGs are) with a pen in the
  corner; a click opens the drawing editor
  (`PageController.openDrawing`: the page is saved, route `drawing`), so does "Edytuj rysunek" in
  the picture view's bar. "/Rysunek", the touch toolbar's Rysunek and the block menu's "Wstaw
  rysunek" (`BlockEditor.insertDrawing` → `PageController.insertDrawing`: an empty drawing stored as
  `Rysunek <yyyy-MM-dd HH.mm>.svg`, embedded at the caret, one undo step) open the editor on a new
  one. Its picture is Brulion's own rendering in the app's theme (the image provider `drawing`,
  "Drawing editor" → Previews: the embed's `url` light, `darkUrl` dark, `EditorHost::drawingUrl`),
  so a drawing on "Jak motyw" turns dark with the app at once. A drawing saved again is shown again
  at once: its URL carries its version (`Workspace::drawingUrl` / `assetUrl` add
  `v=<mtime>-<size>`; Qt's pixmap cache keys by the URL) and the embeds of the page follow the
  file's change (`PageController` on `fileChanged` → `BlockListModel::attachmentChanged`).
* Pictures: Ctrl+V pastes a copied picture as an attachment, files dropped on the page are
  stored in the attachments folder and embedded, "/Obraz…" and the toolbar pick files (the
  pictures: `paths::pictureExtensions()`, docs/SYNTAX.md §2). Opening one (a click on the embed,
  a link to the file) shows the picture itself (`ContentPictureView`, loaded by `PageView` for a
  `pictureRoute` only): fitted into the view with its aspect ratio kept — fitting only shrinks a
  raster picture (a smaller one keeps its own pixels and gets no size toggle), an SVG fills the
  view —; when the fitted picture differs from its own pixels, a click on it or the "Pokaż
  naturalny rozmiar" button (in a view narrower than 600 px — a phone, or on the desktop between
  the sidebar and the side panel — a magnifier icon, its name in the tooltip, on touch that of a
  long press, and for screen readers: as text it left the file name no room) shows those pixels (then
  draggable), the button goes back. Under the
  fitted picture (it moves to the top then) the notes that embed or link it (`ContentBacklinks`);
  the side panel's Linki tab lists them too. The bar shows the file name as plain text. Its
  "Pokaż jako tekst" / "Pokaż obraz" and Ctrl+E (the palette's "Markdown source", the top bar's
  Więcej → Pokaż jako tekst / Pokaż obraz) switch between the picture and the file read as text —
  read-only, for text-like assets such as SVG (a binary picture is mojibake), at most its first
  200 KB (`Workspace.readTextStart` reads no more of the file — a photo is not read whole for
  it — and leaves out whole a character the limit would cut; a note above the text says how much
  of how much), read again when the file changes on disk while it is shown (sync, another
  program: `Workspace.fileChanged`); a mouse selects in it, a finger
  scrolls it (no selection by dragging on touch UIs: such a TextEdit keeps the finger). The editor never loads a picture
  (`page.path` stays ""): its bytes are not a note, and a character typed into them used to be
  saved over the picture. A format the device has no decoder for (`Workspace.canDecodePicture`,
  QImageReader's formats: WebP and AVIF need Qt's image plugins, missing on Android, in the
  browser and in the Nix package) says so ("Obrazów w formacie WEBP nie można wyświetlić na tym
  urządzeniu") instead of a load error — in its view and embedded in a note (`EmbedView`: the
  file's name and the same sentence in the picture's frame, nothing loaded; the embeds'
  `decodable` and `format` from `EditorHost::canDecodePicture`, one helper with the picture
  view: `canDecodePictureFormat`, picturesize.h). `route.open` on a picture: `text`, `links` | `outline` |
  `calendar`.
* Pictures from the internet (`![alt](https://…)`, docs/SYNTAX.md §2): the embeds' entry has
  `web`, `state` ("ask" | "loading" | "ready" | "failed"), `host`, `error` and, once ready, the
  cached file's `url`, `format` (the cached picture's as the host tells it, "PNG", "SVG" — not
  from the URL, a data: URL in the browser has no file name) and size (`EditorHost::webPicture`, the Workspace's
  `workspacepictures.cpp`: fetched into the device's cache, never by QML; an SVG is known by
  that `format`, its URL need not end with `.svg`). `EmbedView` gives a picture not shown yet its
  frame (320 × 180, higher when its text and buttons need it — a phone's narrow column, a nested
  block —, and as high while it loads) with the alt text (at most 2 lines), "Obraz z <host>" on a
  line of its own (never elided away by the alt text), **Pokaż** (`PageController.showWebPicture`;
  **Spróbuj ponownie** with the reason after a failure) and **Zapisz w notatniku** side by side,
  one under the other when they do not fit, centred; a spinner while it loads. A picture fetched
  for the first time has no size until it is there: its row changes height when the fetch ends
  (with the setting below on, also while the journal only scrolls — "Scrolling performance",
  "Still there"); later the size comes from the cached file. A click (a tap) on the shown
  picture opens it on its own in `WebPictureViewer` (`EmbedView.webOpen` →
  `BlockEditor.openWebPictureViewer`), as a click on an attachment opens the picture view: a
  modal popup over the whole window with a bar — "<host> › <file>" (plain text), the size toggle
  (fitted ↔ its own pixels, dragged around; a click on the picture toggles too), **Zapisz w
  notatniku**, **⋯** (the menu below, without "Powiększ") and **Zamknij**; Escape and Back
  (Android) close it. It draws the cached copy as the picture view draws an attachment (fitting
  only shrinks a raster picture, an SVG fills; decoded at most 2048 px a side) — no references,
  no text view. A right-click (its own `WithinBounds` handler: the block menu does not open as
  well) or long press opens `WebPictureMenu` — its first, disabled line "Obraz z <host>" (BMenu
  shows no title), then Powiększ (the viewer), Zapisz w notatniku, Otwórz w przeglądarce (the
  platform's handler), Kopiuj adres obrazu. "Zapisz w notatniku"
  (`PageController.downloadWebPictures`) fetches it when not cached — also while pictures are not
  shown —, stores the attachment and points every picture of that URL on the page at it, one undo
  step; a toast says how many were saved or which failed (a picture edited away meanwhile, or whose
  fetch ends while the page is shown as source, counts as failed, nothing is stored). Pictures in
  blocks that show no embeds (code, formulas, tables, HTML) are text: never fetched or rewritten. The palette's "Zapisz obrazy z internetu w notatniku" (uiAction
  `saveWebPictures`; the page, or in the journal stream the day being written in — today until
  another day gets the caret, as the stream's other page commands) does that for all of them. "/Obraz z
  internetu…" (`EditorDialogs.pickWebImage`) asks for the address (Wstaw only for an http(s) URL;
  otherwise "Wpisz adres zaczynający się od http:// lub https://") with "Zapisz w notatniku"
  checked, every time it opens: `PageController.insertWebPicture` writes `![](url)` (between
  `<…>` when it has blanks or parentheses) at the caret, or as a block of its own after a block that
  is not text. Settings → Edytor → "Pokazuj obrazy z internetu bez pytania"
  (`AppSettings.webPicturesShown`, device-local, off) → `Workspace.webPicturesShown` (not for
  private hosts — those keep "ask" —, once a session per picture; turned off, the fetches it
  queued are cancelled and their rows ask again, "Pokaż" / "Zapisz w notatniku" go on).
  Pasting (`PageController.pasteClipboard`, Ctrl+V and the text menu's Wklej): a picture's address
  alone (a browser's "Kopiuj adres obrazu": an http(s) URL without blanks whose path ends with a
  picture extension, `PageController.isPictureAddress` — by its form, nothing is fetched) goes in
  as the bare address it is, and a toast "Wklejono adres obrazu" offers **Zapisz w notatniku**:
  `PageController.urlToPicture` turns exactly that text into `![](url)` (only while it is still
  there — else "Nie zmieniono: adres zmieniono w międzyczasie") and stores the picture
  (`insertWebPicture` with `download`; in a quote or callout it is replaced where it is too).
  Offered only where the text reads it as a bare address of its own (a `LinkKind::Url` link of
  exactly the pasted range): pasted into a link (`![](…)`, `[x](…)`), an autolink `<…>`, inline
  code, a formula, a comment, a code block or a block that shows no pictures, nothing is offered. HTML of a web page (`htmlToMarkdown`) brings
  its pictures as `![alt](https://…)`; the ones new on the page (not this app's own copy, not
  `data:` pictures) are named in the result (`webPictures`), and a toast "Wklejono %n obraz(y) z
  internetu" offers **Zapisz w notatniku** for them (`downloadWebPictures` each; the result toast
  as usual). The same over selected blocks (`pasteOverSelection`). Nothing is fetched before the
  user asks. A paste that only the system's keyboard or the browser's own paste delivers
  (the text arrives as typed text) offers nothing.
* Mobile toolbar (`MobileEditToolbar`, touch UIs, docked on the keyboard — "On-screen keyboard";
  scrolls sideways): for a block Zmniejsz / Zwiększ wcięcie, Przenieś w górę / w dół, Zadanie (a
  tap: the next status like Ctrl+Enter; held down: the task status menu, above the toolbar), Link
  do strony (`[[`), Tag (`#`), Polecenia (`/`), Termin, Przypomnienie, Wstaw obraz, Rysunek, Cofnij, Ponów,
  Zaznacz (the edited block becomes the selection: the touch selection mode), Więcej (the block
  menu of the edited block, above the toolbar: copying links to it, …), Ukryj
  klawiaturę; for a table cell Cofnij, Ponów, Następna komórka, Więcej (the table's block menu: the
  cell's editing ends, the table is selected), Ukryj klawiaturę; in source
  mode Link do strony, Tag, Polecenia, Cofnij, Ponów, Ukryj klawiaturę; in the touch selection
  mode the selection's tools (`SelectionToolbar`, "Selecting and copying").

### Selecting and copying

One block is edited at a time (its `InlineEditor` selects characters inside it); a selection that
crosses blocks is a **block selection** (Logseq / Notion): whole blocks, each with its subtree —
folded children too —, the same in both note modes (a document's paragraphs, headings and list
items), one run of blocks or blocks picked one by one with gaps between them (bullets 1, 3 and
5). Any characters across blocks — part of one paragraph to part of the next — are selected in
the Markdown source (Ctrl+E, Więcej → Źródło Markdown).

* **Model** (`PageController`, docs/ARCHITECTURE.md): two uids, the anchor (`selectionAnchorRow`)
  and the focus end (`selectedRow`); the selection is the visible rows between them with every
  subtree — a set of uids in `BlockListModel` (`isSelected`, `selectionEdge`: `single` | `first` |
  `middle` | `last` over the visible rows, so a selection with gaps is a band for each run). Blocks
  picked one by one (`toggleSelected(row)`: Ctrl+click, "Pojedynczo") are a list of uids instead,
  none inside another's subtree (a picked parent takes in its picked children; a child of a picked
  block cannot leave alone: "Ten blok jest zaznaczony razem z blokiem, do którego należy"), both
  ends on the one picked last; a range picked into becomes its top blocks first, the block being
  edited joins (as Shift+click starts from it), one left is a range of one again, and a range
  (Shift+click, Shift+↑/↓, a drag, Ctrl+A, ↑/↓) replaces them, from the one picked last.
  `selectionPicked`: two or more picked; `selectionContiguous`: no gaps (picked blocks next to each
  other are a run like a range). The ends and the picked blocks
  follow edits, undo / redo (each step restores the selection it had: an undone deletion selects
  its blocks again — and stops the editing of another block meanwhile: the keys go to the
  selection) and changes from other devices; a zoom ends it; editing ends it; the page's
  properties are never part of a range nor picked. `selectionCount` counts the visible rows
  (folded children come along unseen). A command on the selection (Delete, Wytnij, a paste over it) goes from it to
  the next one without an empty selection between (the touch selection mode stays).
* **Ways in**: Esc in an edited block; Shift+↑ / Shift+↓ (from an edited block once its text
  selection reaches its start / end); Ctrl+A twice (Notion); Shift+click on any block — its text,
  a code or calculation frame, a table, a query, an embed, a rule, a bullet or number
  (`BlockEditor.blockTapped`, the click's modifiers) —, and Ctrl+click on any of them to pick that
  block into the selection or out of it (`BlockEditor.toggleBlock`, desktops); a drag
  with a mouse, touchpad or stylus from one block into another (`BlockEditor`'s tracker — a
  passive `PointHandler` over the list, the state in the editor because delegates may be destroyed
  meanwhile — scrolls near the view's edges; a left drag no longer scrolls the list on non-touch
  UIs, the wheel and the touchpad do — also in the journal stream); the block menu's "Zaznacz".
  Touch UIs: a **long press on a block** — its text, a code or calculation frame, a table cell, a
  query, beside an embed (not the block being edited: there the system's word selection and
  handles stay) — enters the **selection mode** — the keyboard goes, the selection's
  tools dock where the editing toolbar is; moving the finger without lifting it extends the
  selection (the list holds still, scrolls at its edges); taps on blocks move the selection's end
  there instead of editing; two round **grips** (44 px) at the band's start and end move its
  ends (a selection without gaps); the tools' **"Pojedynczo"** (a toggle, `BlockEditor.pickMode`)
  makes taps — and long presses — pick single blocks into the selection or out of it instead (no
  grips meanwhile; off again, a tap moves the end from the block picked last); "Gotowe" and Back
  leave it (and "Pojedynczo" with it). The editing toolbar's "Zaznacz" and the bullet menu's "Zaznacz"
  enter it too, and so does a hardware keyboard's Esc or Shift+↑/↓ on a touch UI (the same tools
  whichever way the selection was made).
* **Across journal days**: the journal stream selects blocks of several days at once
  (`StreamSelection`, brulion_editor, one per stream; each day's `PageController` is attached
  while its delegate exists). Ways in: Shift+click on a block of another day (from the block
  selected or edited in one day); Shift+↑/↓ past a day's first / last block — also from an edited
  block's first / last line — into the previous / next day (days with nothing to select are
  passed); a mouse or finger drag from one day into another (`BlockEditor.dragTarget()` asks the
  stream's `dayAt(scene)`); in the touch selection mode a tap on a block of another day. The span
  runs from the anchor block to the focus block: the anchor's day from the anchor to its end (the
  focus below) or from its start to the anchor (above), the focus day symmetric, the days between
  whole — never page properties; each attached day selects its part (its own band), `count` and
  "Zaznaczono %n" are the stream's. It is paths and rows, not controllers: a day scrolled far away
  is destroyed and gets its part again when it comes back (`attach()` once loaded); meanwhile a
  copy reads it from its file. While dragging, the stream scrolls past the pressed day only as far
  as that day stays in the list's cache (half of it: `journals.selectionReach`) — longer spans:
  the wheel and Shift+click. **Copy only**: Kopiuj / Ctrl+C / Ctrl+Insert and Więcej → Kopiuj
  jako tekst take every day's blocks (`StreamSelection.copy()`), each day's after its link on a
  line of its own and a blank line (`[[2026-10-27]]`; plain text: the day's long date,
  "wtorek, 27 października 2026"), a blank line between days, one HTML flavour rendered on
  request, the same 800 KB rule; the toast says "Skopiowano %n bloków z kilku dni". Wytnij, Usuń,
  Pojedynczo, the indents, moves and task statuses are shown unavailable (`BIconButton` /
  `BMenuItem.unavailable`, the reason as tooltip / description) — so are Wytnij, Usuń, the
  indents and moves of the block menu of a block in the span, whose Kopiuj / Kopiuj jako tekst
  copy the whole span (its count in the label) —, and they, Delete, Ctrl+X, Ctrl+V, Tab, Alt+↑/↓ and
  Ctrl+Enter say "Na kilku dniach naraz działa tylko kopiowanie"; Zaznacz wszystko / Ctrl+A select
  the day's own blocks. The tools show on one day only — the focus day's (`toolsController`; the
  anchor day's while the focus day is away), which becomes the active day; the grips are hidden.
  Using one day on its own ends the span and clears the other days: a click or tap that edits,
  Ctrl+click, a selection made in that day alone, folding (any selection change the stream did not
  make), and a day leaving the list with an end of the span; Esc, Gotowe, × and Back clear every
  day. Shift+↑/↓ with the focus day scrolled away scrolls there first (`revealRequested`). With
  every day of the span scrolled away (no tools shown: `toolsController` null) a tap edits as
  usual, which ends the span — no tap grows a span the user cannot see.
* **The band**: one band over the range from the editor's left edge (a block alone keeps its
  outline: from its own level, framed), rounded at its ends, a line down its left edge — blocks
  picked with gaps between them: a band for each run; made only for selected blocks (a `Loader`:
  "Scrolling performance").
* **Tools**: `SelectionToolbar` — "Zaznaczono %n", Kopiuj, Wytnij, Usuń, Zaznacz wszystko,
  Pojedynczo (docked only), Więcej (Kopiuj jako tekst, "Kopiuj odnośnik" for one block, Zwiększ /
  Zmniejsz wcięcie, Przenieś w górę / w dół, Status zadania; the indents only where
  they can happen — `canIndentSelection()`: a document's paragraphs and headings have none; with
  gaps the indents and moves are shown disabled — `BMenuItem.unavailable`, the reason in a tooltip
  — and a tap says why in a toast: "Wcięcia i przenoszenie działają tylko dla bloków zaznaczonych
  bez przerw"), Gotowe. Docked in the editing toolbar's slot on touch
  UIs (the `selection` mode of `BlockEditor.toolbarMode`; on phones right on the bottom
  navigation, no keyboard), a
  compact bar floating at the top of the view on desktops while two or more blocks are selected
  (its × clears the selection).
* **Copying**: `copySelection()` puts the blocks' **Markdown** on the clipboard as text/plain — the
  top blocks in document order, also with gaps between them —, as written, each top block re-based
  to column 0 (visual columns: tabs and spaces keep their
  nesting), LF line breaks, no BOM, no page properties (`markdown::blocksMarkdown`) — and, from the
  host, escaped semantic **HTML** as text/html (`io::clipboardHtml`: lists, headings, checkboxes,
  tables, dates, recurrence and priorities of any block; links to notes as their text, pictures as
  links to their files, never data: URLs; marked `<meta name="generator" content="Brulion">`), so
  a paste into mail or an office app is formatted and one into Obsidian, a plain field or Brulion
  is Markdown. The text/html flavour is advertised at once but rendered only when an application
  asks for it (`LazyHtmlMimeData`: `retrieveData()`, once, kept; a paste into Brulion, a code
  editor or a terminal never renders it; Ctrl+C on a whole 5000-block page takes ~4 instead of
  ~40 ms, docs/PERFORMANCE.md) — on Linux desktops (xcb, Wayland) without a size limit of its own; the
  Markdown is a snapshot, `((id))` texts and links are resolved when the HTML is made, and when the
  app quits while the copy is still the clipboard's, it is made first (X11's hand-over to a
  clipboard manager reads every flavour during shutdown). Android, the browser and Windows take
  the data inside `setMimeData()` (`clipboardReadsAtOnce()`; Windows: `OleSetClipboard` enumerates
  the formats and Qt offers CF_HTML only for a non-empty `html()`): rendered at once there, kept
  only while both flavours fit 800 KB. On Linux also the primary selection. "Kopiuj jako tekst": plain text ("• ",
  numbers, ☐ / ☑ / ☒, emphasis marks in pairs gone, code spans verbatim, `((id))` as its block's
  text). A copy is read back (never "Skopiowano" for a clipboard that did not take it — except in
  the browser, docs/WEB.md "Schowek") and refuses more than 800 KB (Android's clipboard takes
  about 1 MB per transaction) with a toast — never cut short. Cut = copy, then delete as one undo
  step. The toasts "Wycięto %n bloków" and "Usunięto %n bloków" (two or more blocks) have
  "Cofnij": the editor's `message(text, kind, action)` carries `{text, run}`
  (`BlockEditor.undoAction()`), the page views pass it to `AppController.toastAction` and the
  shell's toast shows the button; it runs `PageController.undoStep(token)`, which undoes that edit
  only while it is still the page's last step (`undoToken()` taken right after it names the
  history, the position and the step there) — after other edits (one made in its place after
  Ctrl+Z too), on another page or once undone it says "Nie cofnięto: strona zmieniła się od tego
  czasu".
  A toast's button takes no focus when pressed: the editor keeps the keys.
* **Acting on it**: Tab / Shift+Tab, Alt+↑/↓, Ctrl+Enter, Ctrl+Shift+Enter (the status menu) and
  the toolbar's Status zadania act on the range from one end to the other — on the picked blocks
  of a selection with gaps (Ctrl+Enter and the statuses; Tab and Alt+↑/↓ only without gaps, else a
  toast says why); a block selected alone keeps the block's own keys — Ctrl+Enter changes it, not
  the children it brings along (they are copied, cut, moved and deleted with it). Delete, Wytnij
  and a paste over the selection take all of it, gaps too (one undo step; a paste goes where the
  first selected block was).
* **Pasting**: Ctrl+V in an edited block splits Markdown into blocks (`pasteText`, relative nesting
  kept); one copied list item brings its text, not a second marker (into an empty bullet a task
  keeps its checkbox, a code item its fence; over a whole paragraph the paragraph becomes the
  item); HTML from other apps (a browser, an office suite) whose text does not look like Markdown
  comes as Markdown (`EditorHost::htmlToMarkdown`, io's converter); this app's own copies paste as
  their Markdown (the last one, or HTML with the generator mark — also after a restart; text with
  block ids, properties, task signifiers, `$$` or footnotes counts as Markdown). With blocks
  selected the paste replaces them (the pasted blocks are selected, one undo step); a pasted
  note's YAML frontmatter is left out — nothing else to paste: nothing is deleted —, and with the
  page's properties selected the paste goes in below them (they are never replaced). Block ids
  (`^id`, `id::`) that already resolve elsewhere are dropped from what is pasted — `((id))` is
  vault-wide —, except right after a cut (in any page of the app, a block selection or an edited
  block's text: a move — unless the cut page has them again, the cut undone) and for the replaced
  blocks' own ids.
* **Inside an edited block**: Ctrl+C / Ctrl+X and the text menu copy the exact characters
  (`PageController.copyEditText`; TextEdit's own copy turns no-break spaces into spaces), the
  selection stays shown while a menu or dialog has the keyboard (`persistentSelection`), and a
  right-click opens Wytnij, Kopiuj, Wklej, Zaznacz wszystko and Blok… (desktops; touch UIs have the
  system's handles and popup).
* **Part of several blocks** (the end of one paragraph and the start of the next): select the
  whole blocks and copy them, or select exactly those characters in the Markdown source (Ctrl+E,
  Więcej → Źródło Markdown) and copy them there.
* **Browser**: docs/WEB.md "Schowek".
* **Tests**: core `tst_markdown_blockclipboard`, `tst_markdown_blockdoc` (ranges), `tst_io_html`;
  controller `tst_editor_selection` (the real clipboard read back; the HTML rendered only when
  asked for, once; blocks picked one by one, their copy, cut, delete, paste, undo, statuses, no
  moves with gaps); `tst_editor_streamselection` (the parts of a selection across days, keys,
  taps and drags over the days' edges, days destroyed and created again, the copy with the days'
  links); the journal stream's `journals-span-selftest` with real input (`tst_content_journals_span_qml`
  on a desktop, `_touch` on a phone); the playground's `selectiontest`
  with real keys, mouse and touch (`tst_editor_qml_selection` — Ctrl+clicks —, `_touch` —
  "Pojedynczo"); the journal stream's self-test; `scripts/android-emulator-test.sh --selection`.

## Drawing editor (`qml/drawing`, `src/app/drawing`)

Route `drawing` (`{path}`): `DrawingView` on a drawing file (docs/SYNTAX.md §2 "Drawings": an SVG
with an embedded Excalidraw scene, a `.excalidraw` scene, a drawing of Obsidian's Excalidraw plugin
`*.excalidraw.md`; core module `drawing`). For handwritten notes on a tablet (lectures, sketches)
and diagrams (shapes, arrows, text), on every platform.

* **Files:** each is saved in its own format (`DrawingCanvas.format`: `svg` | `excalidraw` |
  `excalidrawMd`; `fileBytes()` → core `drawing::writeDrawingFile`): an SVG as Brulion writes it, a
  `.excalidraw` scene as Excalidraw does, a plugin file with only its `## Drawing` block and the
  entries of text changed or removed here rewritten — the frontmatter, the back of the card and the
  plugin's other sections as they were; elements erased here written as deleted ones, so Obsidian,
  showing the drawing, removes them (after a save the file written is the base of the next:
  `markSaved`). New drawings are SVGs. A plugin file opened as a page (from the page list, a link,
  search) is shown as its drawing: `PageView.drawingPage` → the picture view, its "Edytuj rysunek"
  opens this editor, "Pokaż jako tekst" the plugin's Markdown read-only — never the block editor.

* **The paper** (`DrawingCanvas`, a `QQuickPaintedItem`): the file's paper
  (`appState.viewBackgroundColor`, white), plain, lined (32 px) or squared (24 px), endless in every
  direction; the picture written covers the elements plus a margin. Opened: the elements from their
  top left at 100 %, zoomed out to the view's width when they are wider (a phone); a zoom other
  than 100 % is shown in the corner (`#535a6a` on light paper, `#9ba1af` on dark: 6.9 / 7.2:1).
* **Light or dark paper** ("Tło rysunku", per drawing: `DrawingCanvas.theme`, saved in the file as
  `appState.brulionTheme`, an undo step that marks it modified): **Jak motyw** (`auto`, the
  default: follows the app's theme — `appDark: Theme.dark` —, at once, also while a stroke is being
  drawn; the app's theme is no edit), **Jasne**, **Ciemne**. The stored colours never change: the
  dark paper (`#121212`) shows the strokes in Excalidraw's dark-mode colours (`drawing/palette.h`:
  the ink `#1f2430` as `#caced8`, every pen colour ≥ 4.2:1 on the paper; the lines and the grid
  `#323a45`), highlighter strokes in their own colour at 1.25 × the opacity (≥ 3.2:1 on the paper,
  the dark ink on them ≥ 2.9:1), the eraser's ring light (`DrawingCanvas.dark`, `paperColor`). The
  tool bar's swatches show the colours as the paper shows them (`displayColor`); what is chosen
  and remembered is the colour as drawn. The file's picture follows the choice (docs/SYNTAX.md §2:
  dark attributes for Ciemne; for Jak motyw an `@media (prefers-color-scheme: dark)` style sheet,
  so browsers show it dark when their viewer prefers dark, Obsidian in its dark theme). A merge
  with another device's version keeps the theme chosen here only when it was changed here since
  the last load or save (as the paper's pattern). No keyboard shortcut (Excalidraw's Alt+Shift+D
  switches its app's theme; this one is per drawing).
* **Input:**
  * a **stylus** works the tool — draws with its pressure (the line widens as it presses harder;
    a stylus that reports one pressure only draws like a mouse), draws the lasso or a shape —; its
    **eraser end**, or the barrel button held, erases whatever tool is chosen (in the browser,
    whose pen is one device, the eraser end comes as the pen's `Qt::ExtraButton3` — the web's
    button 5 —, the barrel button as `Qt::RightButton`; Qt for Android names the eraser end with
    Qt 5's pointer type 3 and passes `BUTTON_STYLUS_PRIMARY` as `Qt::ExtraButton3`). The pen's
    release reports pressure 0 (on Android a pixel beside the last move): a line ends with the
    pressure it had. Qt Quick hands an item no tablet events (only pointer handlers
    get them, and Qt makes mouse events of the unhandled ones — X11, the browser and Android too:
    a line without pressure, as a mouse draws it), so the canvas takes the stylus's tablet events
    from its window (an event filter) when
    it is the topmost item taking input under the pen: a menu, a popup, a toast or a bar over it
    gets the pen as before, and so does any modal popup that is open (the drawer opened). Qt Quick
    Controls' popup layer (`QQuickOverlay`), visible over the whole window whenever a Drawer exists
    and taking every button, counts only where an open popup is and where an item put there takes
    input — a toast, not the window-wide host of the toasts (`tst_drawingcanvas`
    `popupsOfQtQuickControls`, `itemsInTheOverlay`; the host took the pen from the canvas in the
    real app until the browser tests found it). Mouse events the system makes of the pen itself (Windows) are
    recognised by their time and place and left alone;
  * a **mouse** works the tool with the left button (Shift: a square or a circle, lines at 15°
    steps; with the select tool Shift adds to the selection or takes away); the middle or right
    button — or the left one with
    Space held — moves the paper; the wheel scrolls it (Shift: sideways), Ctrl+wheel and a
    touchpad's pinch zoom at the pointer (10 %–1000 %);
  * **fingers:** with "Rysuj palcem" one finger works the tool (draws, erases, draws the lasso or a
    shape, moves the selection, taps text), else it only moves the paper — a palm resting on a
    selection never drags it; two fingers move and zoom — a second finger that lands while a
    stroke has just begun makes it the gesture (the stroke is dropped; a lasso or a shape being
    dragged too). A long press does nothing special. "Rysuj palcem" is on by default until a stylus
    has touched the paper on this device (AppSettings `drawing/stylusSeen`): from then on a palm
    or a finger never draws; the toggle (the tool bar on wide touch screens, Więcej on phones)
    makes it the user's choice (`drawing/finger`: `auto` | `on` | `off`);
  * **palm rejection:** while the pen is down every touch is ignored — it neither draws, moves
    the paper, nor ends, cuts or erases the pen's stroke —, and so is a hand that stays after the
    pen lifts, until every finger has lifted. A pen that lands while fingers move the paper or
    draw ends their gesture (a finger stroke just begun is dropped) and draws.
* **Tools** (`DrawingToolbar`; above the paper on wide windows, at the bottom on phones — in
  the bottom navigation's place, `toolbarDocked` and `replacesBottomNav` —; when one row cannot
  hold them all — a phone held upright, `twoRows` — the tools and the swatch take a line of their
  own, centred, above Cofnij, Ponów, Więcej and Gotowe, so nothing hides past the bar's edge; a row
  that still does not fit scrolls sideways): Cofnij,
  Ponów, **Pióro** (P), **Zakreślacz** (H: wide, translucent, an even width — constant pressure,
  as Excalidraw draws it alike), **Gumka** (E: removes every element it touches — a shape with its
  label, never a locked one nor a locked shape's label —, one undo step per gesture, a ring
  follows the pointer),
  **Zaznacz** (V, a lasso icon; below), **Kształty** (one button showing the last shape — R
  Prostokąt, O Elipsa, D Romb, L Linia, A Strzałka, remembered in `drawing/shape` —; clicked again
  while a shape is the tool it opens their menu), **Tekst** (T; below), the current tool's colours
  (pen, shapes and text: atrament, niebieski, czerwony, zielony, pomarańczowy, fioletowy;
  highlighter: żółty, zielony, różowy, niebieski, pomarańczowy) and widths (cienka / średnia /
  gruba; wąska / średnia / szeroka; text: its sizes Mały / Średni / Duży / Bardzo duży, 16 / 20 /
  28 / 36 — an "A" at four sizes, `drawing/fontSize`) — on phones behind one swatch, a popup above
  the bar —, Papier (gładki, w linie, w kratkę: an undo step; and the backdrop's entries, below),
  **Tło rysunku** (a sun-and-moon, sun or moon icon: Jak motyw / Jasne / Ciemne; on phones in
  Więcej: "Tło jak motyw", "Jasne tło", "Ciemne tło"), Rysuj palcem (touch screens), Pokaż cały
  rysunek (Ctrl+0; Ctrl++ / Ctrl+- zoom) and **Gotowe**
  (saved, back). Colours and widths are remembered on the device (`drawing/…`). Undo: Ctrl+Z,
  Ctrl+Shift+Z / Ctrl+Y; every gesture and action is one step (a generic diff of the elements,
  `drawing::ElementDiff`, at most 500 steps); the history is the session's (a reload clears it).
  Undo and redo stamp what they put back as a new edit (a higher `version`, a new `versionNonce`
  and `updated`), so another device's merge and the Excalidraw plugin never take it for older.
* **Shapes** (rectangle, ellipse, diamond, line, arrow): dragged out, shown while dragged; the
  colour is the pen's, the stroke twice the pen's width (1 / 2 / 4, Excalidraw's thin, bold, extra
  bold); Shift: a square or a circle, a line at 15° steps; a click (under 3 px) makes none.
  Rectangles get Excalidraw's rounded corners, other shapes sharp; no fill. The tool stays (several
  shapes in a row) and the new shape is not selected. New arrows are never bound to shapes.
* **Selection** (Zaznacz, V): a lasso (dashed, in `Theme.info`, a 10 % fill) takes every element at
  least half inside it (its points; a box's corners, edge middles and centre), a tap the topmost
  element under it (8 px, 14 px for a finger or a pen; an unfilled shape only near its outline) —
  with its group (`groupIds`, the outermost) and a shape's label; a tap on nothing clears it.
  Locked elements — and the labels of locked shapes — are never taken. The selection is framed (dashed around all, thin around each
  when there are several); a drag inside the frame moves it (one undo step; arrows bound to it
  follow, labels go along). **DrawingSelectionBar** floats centred above the frame (below it when
  there is no room, always inside the view), touch-sized on touch screens, while nothing is being
  drawn, moved or written: the pen's colours (recolour; on narrow windows one swatch opening them
  — a highlighter stays one), **Duplikuj** (Ctrl+D: copies 10 units right and down, selected),
  **Na wierzch** (Ctrl+Shift+]), **Na spód** (Ctrl+Shift+[: above a locked backdrop), **Usuń**
  (Delete / Backspace). Keys: Ctrl+A (everything), Escape (a lasso or a shape in progress dropped,
  else the selection cleared), the arrows (move it 1 unit, with Shift 10), Ctrl+] / Ctrl+[ (one
  step up / down). Another tool clears the selection; undo, redo and a merge keep the selected
  elements that still exist.
* **Text** (Tekst, T): a tap on the paper writes a new text there (its first line's middle at the
  tap), a tap on a text edits it, a tap inside a shape writes its label; with the select tool a
  double tap does the same. **DrawingTextEditor** opens on the paper in the text's font, size
  (× zoom), colour (as the paper shows it) and alignment — as wide as the text, a label as wide as
  it wraps in its shape (Excalidraw's getBoundTextMaxWidth: a rectangle's width, the widest
  rectangle inside an ellipse — width / 2 · √2 — or a diamond — width / 2 —, less 2 × 5), centred
  —; Enter starts a new line; Escape, Ctrl+Enter, a press on the paper beside it or (touch
  screens) its "Gotowe" button write it — one undo step; an emptied text is deleted; a label grows
  its shape when it needs the room (as Excalidraw grows it, from its top left). A text closed as it
  was is no edit: nothing is stamped, wrapped again in Brulion's fonts, saved or added to the undo
  history. A locked shape's label is not edited (the text tool writes a new text there). On a phone the paper
  moves so the text stays above the keyboard (`DrawingCanvas.ensureVisible`, `KeyboardInsets`).
  New text: Excalidraw's Helvetica (sans in both apps) in the app's UI font, line height 1.15.
  While text is written the canvas takes no other input and the view's keys rest.
* **Backdrop** (Podkład — notes on slides and PDF pages; docs/SYNTAX.md §2 "Backdrop", core
  `drawing/backdrop.h`): a picture, an exported slide or a PDF page under the ink. **In a note:**
  the slash command **"/Rysunek na obrazie lub PDF…"** (`drawingOn`: also "slajd", "slajdy", "pdf",
  "podkład", "wykład", "tło", "slide", "annotate", "backdrop") and the block menu's **"Wstaw rysunek
  na obrazie lub PDF…"** open the file picker ("Obrazy i PDF"; `FilePicker` — the browser: its own
  picker, pictures only, `DrawingBackdrops.pickInBrowser`); the file is taken into the notebook
  (`EditorHost::importAttachment`, `Workspace::importAttachment`: a file of the notebook stays
  where it is — on Android also when the system's picker names it by a `content://` URI of the
  phone's storage, a memory card or Downloads' path, `platform/documentpaths.h` —, the same file
  already in the attachments folder — also under a " 2" name — is used again, anything else is
  copied there as pictures are). When no drawing comes of the pick (the page picker cancelled, a
  locked or unreadable PDF, a picture this device cannot decode) the copy made for it is deleted
  again (`PageController.discardBackdropSource` / `Workspace.discardImport`: only a copy that pick
  stored, never a file of the notebook or one reused). A picture becomes one drawing ("slajd
  (rysunek).svg", light — **Jasne**) and opens in the editor; a PDF opens
  **DrawingBackdropDialog** (a centred card, a bottom sheet on phones): the file's name, "N stron",
  the pages as thumbnails (the image provider `pdfpage`, `PdfPageImageProvider`:
  `image://pdfpage/<file>?page=N&v=…`, drawn as they scroll into view; every cell the first page's
  shape — `DrawingBackdrops.pdfInfo` `width`/`height` —, wider cells for landscape slides), a tap
  chooses or drops a page — with the keyboard Tab reaches the pages, the arrows move (a focus
  ring), Space or Enter chooses; in the one-page dialog the arrows choose at once —,
  **Wszystkie** / **Żadna**, the field **Strony** ("1-3, 7", "wszystkie"; in step with the
  thumbnails), at most 60 pages ("Najwyżej 60 stron naraz."), **"Wstaw rysunki (N)"**: one drawing
  per page ("wykład 3 s. 4.svg", as the app — **Jak motyw**), the first at the caret, every
  further one in a block of its own after it, in order — one undo step
  (`PageController.insertDrawingsOn`; the files stay, as with "/Rysunek"); one drawing opens in the
  editor, several stay in the note with a toast ("Wstawiono N rysunków"); when another page was
  opened in that editor while a picture was prepared, nothing is inserted (a toast; the imported
  copy goes). The pages are prepared
  in the background (`DrawingBackdrops.prepare`: Qt's thread pool, the browser one per event-loop
  turn) with a progress bar ("Strona 3 z 12…") and **Anuluj** — inserted drawings' files made
  there too (`prepare(…, drawings)`: core `newDrawingOnBackdrop` written — the raster's two base64
  copies and the scene's deflate), so the GUI thread only stores them and 60 slides freeze no
  window; a PDF with a password ("Ten PDF
  jest chroniony hasłem — Brulion go nie otworzy."), one that cannot be read and a picture this
  device cannot decode say so. **In the editor:** the paper's menu (on phones Więcej) has
  **"Podkład z obrazu lub PDF…"** (set or replace: a PDF's page in the same dialog, one page,
  **"Ustaw podkład"**), **"Inna strona PDF…"** (when the backdrop is a PDF's page and PDFs can be
  drawn here; the file renamed or gone: "Pliku … nie ma już w notatniku" — renames do not follow
  into drawings) and **"Usuń podkład"** — each one undo step (`DrawingCanvas.setBackdrop`,
  `removeBackdrop`; `hasBackdrop`, `backdropPath`, `backdropPage`). A thin progress line runs
  over the paper while a picture is prepared; one ready during a stroke, a shape or a text is set
  when that ends, one for a drawing closed meanwhile is dropped (its imported copy too), and one
  that would make the file unreadable is refused with a toast ("tooLarge"). The backdrop is locked: the
  eraser, the lasso, a tap and Ctrl+A never take it, "Na spód" stops above it. In the dark it is
  shown **"document dark"** (Excalidraw's dark filter per pixel, `darkImage`, decoded once per
  backdrop: a white slide `#121212`), so the ink mapped light stays readable; a Ciemne drawing's
  file gets that dark raster in its picture (encoded once, `drawing::SvgOptions`), a Jak motyw one
  no dark style sheet. The raster: the picture's own bytes when it is a PNG or a JPEG within 2400 px
  and 4 MB that needs no EXIF turning, else decoded (EXIF turned, the first frame of a GIF, an SVG
  — a drawing too — drawn 2400 px on its long side) and stored as the smaller of PNG and JPEG
  (`backdropimage.h`); a PDF page drawn 2400 px on its long side (at the resolution of that size,
  `pdfRenderDpi`: a page larger than A4 costs no more memory). The previews decode a backdrop
  at the size they show it; the editor keeps decoded only the rasters its elements show (a
  backdrop replaced: its raster and dark twin dropped, decoded again on undo).
  **PDF renderers** (`src/platform/pdfpages.h`, chosen by CMake): Android — the system's
  `PdfRenderer` (JNI, `PdfPages.kt`; nothing added to the APK); Windows, the AppImage and CI's
  Linux build — Qt PDF (pdfium; aqt's `qtpdf`); the Nix package — poppler-qt6 (nixpkgs has Qt PDF
  only inside QtWebEngine); the browser — none (Qt has no Qt PDF for WebAssembly): there pictures
  only, while drawings on PDF pages made elsewhere show and edit normally (their pixels are in the
  file). Sizes: docs/PERFORMANCE.md "Rozmiar".
* **Saving:** by itself, 1.2 s after the last change, and when the route changes, the view goes
  (Back, Gotowe, another view) and the app leaves the foreground (`Workspace.writeAttachment`:
  atomic, the replaced version kept in History as for notes). A text being written goes in first
  on those saves; the 1.2 s timer never commits it — it waits until the text's editor closes (a
  commit mid-word would close the editor and hand the next keys to the tools' shortcuts). A failed write is said by the
  shell's error toast. Every save reads the file first: a change another program made that has
  not been reported yet (a folder watch that missed it, Android's shared storage) is taken in as
  below, never written over.
* **Changes from elsewhere** (Syncthing, Nextcloud, another program) while the drawing is open:
  with nothing changed here the other version is shown; else they are merged element by element
  (`DrawingCanvas.takeExternal` → core `drawing::merge` against the last load or save: the other
  version in its order, plus the elements drawn here since — on top —, the ones changed here
  replacing theirs, minus the ones erased here; what was deleted there stays deleted; the paper's
  pattern and its light or dark theirs unless changed here — the same rule as the sync center's
  "Scal rysunki", which has no common version: a union, see "Sync visibility" → "Conflict copies
  of drawings"; Nextcloud's sync merges against the version both devices had last)
  and saved, with a toast. A gesture in progress goes on and ends on the new version. A version
  Brulion cannot edit any more (a frame or an embedded page added in excalidraw.com) is
  shown as it is; what was drawn here and not saved is written to a new file beside it ("… (to
  urządzenie).svg" — or `.excalidraw`, `.excalidraw.md`: the file's own format —,
  `Workspace.createAttachment`), with a toast naming it. A plugin file's Markdown is theirs then
  (their back of the card, their text entries), the merged scene in it. The canvas's own saves
  coming back are recognised (`isKnownFile`). A rename follows the file (the route is applied one
  event-loop turn late, as in PageView); a file deleted while open is never written again ("Rysunku
  … nie ma w notatniku").
* **Files Brulion does not edit** are shown as the picture they are, with why and "Otwórz jako
  obraz" (the picture view): an Excalidraw drawing with frames or embedded web pages (or an
  element of a kind Brulion does not know: it would lose them — "ma ramki lub osadzone strony
  internetowe z Excalidraw"), an SVG without a scene, a scene that cannot be read, and elements
  farther than 10⁶ units from the origin (`drawing::CoordinateLimit`: the paper's lines and the
  file's size stay bounded; the editor's own elements stay within it). Nothing is written for
  them. A drawing of Obsidian's Excalidraw plugin with frames or embedded pages says to change it
  in Obsidian ("ma ramki albo osadzone strony, których Brulion jeszcze nie edytuje"), one in the
  plugin's oldest form (the scene on one line, no code block: `DrawingCanvas.legacy`) to open it
  there once. Excalidraw drawings with shapes, arrows, text and pictures are edited (docs/SYNTAX.md §2
  "Elements": drawn clean — the hand-drawn look is kept in the file, not drawn —, text in the
  app's fonts, elbow arrows moved rigidly).
* **Rendering:** every element is prepared once (`drawingrender.h` `prepare`: its paths from core
  `geometry.h` `elementShape` and its colours light and dark, kept while its version stays; a
  freehand line moved as a whole keeps its outline, moved); the committed elements are rendered on
  the GUI thread into a cached image of the view (600 strokes of 60 points: ~7 ms on the laptop;
  600 shapes, arrows and texts: ~2 ms); while the paper moves the cached image is drawn moved and
  scaled and rendered again once the gesture rests; the stroke being drawn, a shape being dragged,
  a moved selection (hidden in the cached image meanwhile), the lasso and the selection's frame are
  drawn on top. `paint()` (maybe on the render thread) draws a snapshot taken in
  `updatePaintNode()`, while the GUI thread waits. The paper and the elements are painted by
  `drawingrender.h` (`paintPaper`, `paintElement`: fills, hatching, outlines with their dashes,
  arrowheads, text in the fonts `DrawingFonts.set(Theme.fontUi, Theme.fontMono)` names — families
  3 and 8 monospace —, pictures decoded once from the scene's files or a grey placeholder), shared
  with the previews.
* **Previews** (the embeds in notes, the picture view, the notice of a file Brulion does not edit):
  the image provider `drawing` (`DrawingImageProvider`, registered in `main.cpp`):
  `image://drawing/<absolute path, percent-encoded>?v=<mtime>-<size>&dark=0|1`
  (`Workspace.drawingUrl(path, Theme.dark)`; `EditorHost::drawingUrl`, the embeds' `url` and
  `darkUrl`). A drawing Brulion draws is painted by `renderDrawing` in the theme the drawing and
  the app give, in the frame of the size the file's SVG root names — what the embed, the picture
  view and the notice are sized by from the start (`editor::pictureSize`): Brulion writes its
  `pictureRect` there, a file excalidraw.com wrote has its own margins, and the strokes are centred
  in it at one unit a pixel (smaller only when they do not fit), so the frame never changes shape
  when the picture has loaded —, fitted into the requested size, at the screen's pixel ratio. The
  paper is always opaque (`displayPaper`: a see-through `viewBackgroundColor` as over white).
  Anything else (an Excalidraw drawing with a frame, a scene that cannot be read) is the file's own
  picture through Qt SVG, turned dark in a dark app colour by colour as Excalidraw's dark mode does
  (`drawing::darkSvg`: the colours of its attributes and style sheets; pasted pictures and colour
  emoji keep theirs; the per-pixel filter `darkImage` only for a file that mapping cannot read) —
  unless it is dark already (its edge darker than mid grey: a "Ciemne" drawing whose scene cannot
  be read keeps its dark picture). A `.excalidraw` scene or a plugin file has no picture of its
  own: one Brulion draws is rendered so in the frame of its `pictureRect` (the size
  `editor::localDrawingSize` gives the embeds and the picture view, on the GUI thread, once per
  version of the file: a 300 KB plugin file of 1500 strokes ~45 ms on the laptop, mostly Qt's JSON
  parser — `tst_editor_picturesize largeDrawingSizeIsCached`); one it does not draw (frames)
  shows the plugin's export beside it (`Szkic.excalidraw.svg` / `.png`, `editor::drawingExportOf`)
  or else its elements with a dashed grey box for each of the others (`drawing::previewDrawing`) —
  never a blank picture. A `.md` file is read only when it is a plugin drawing. It runs in Qt
  Quick's image reader thread (the browser: the GUI thread) and only reads the file. A theme switch loads the other URL; `retainWhileLoading` keeps
  the shown picture — and the embed's frame its size — until it is there.
* View contract: `title`, `editing` (no capture button over the paper), `toolbarDocked` (phones),
  `edgeSwipe` (no drawer edge strip: a stroke from the left edge stays a stroke).

Tests: `tst_drawing` (core: the format, Excalidraw's own fixtures, the outline, the SVG, the dark
colours — of another program's SVG too), `tst_drawing_exmd` (LZ-String against lz-string's own
vectors and the fixture generator's port, malformed and random input; the plugin's files —
compressed, json, CRLF, commented text, a frame, the oldest form — read, written back with every
byte around the block kept, deleted elements, text entries following edits — a text only moved,
recoloured or carried by its shape keeps its Markdown (links, transclusions), also across saves —,
crafted files read in linear time, the one API of drawing files, the `.excalidraw` layout — type
first, still a drawing beyond 4 KB), `tst_drawing_elements` (every kind of element read —
`excalidraw_shapes.*` —, malformed ones normalised or foreign, written back byte for byte, a
changed one's version, new elements as excalidraw.com reads them, fractional indices, corners,
arrowheads, hatching, hit tests, the lasso, every edit, undo diffs of random edits, the SVG of
shapes and text, random elements), `tst_drawing_merge` (the merge: new here on top, changed here
wins, deleted here / there, a backdrop at the bottom, an unknown base, versions, Polish ids, random
merges keeping their invariants; how two versions relate — counts, the same drawing, an older one;
Nextcloud's merge of files: SVG, `.excalidraw`, plugin files with their Markdown, and everything it
refuses — a frame, a broken scene, no base, other formats, an element changed without a new
version, both sides' Markdown changed), `tst_drawingcanvas` (a preview no input reaches, stylus, eraser end — also the browser's `ExtraButton3` and Qt for Android's pointer type 3 —, the release's pressure, barrel button, a popup over the paper, the toasts' host in the overlay, Windows' mouse
events of the pen, fingers, a pinch, a palm while the pen is down, mouse buttons and the wheel,
undo, the SVG checked with Qt's SVG renderer, loading, merging — also during a stroke, the paper,
the theme —, far-away strokes, painting light and dark, the app's theme as no edit, a stroke drawn
while the theme changes, a see-through paper, Qt SVG ignoring the dark CSS, the image provider —
light, dark, an Excalidraw drawing turned dark (a pasted photo keeping its colours), a freehand
one in the file's own frame, a scene that cannot be read (a dark one kept dark), refused ids,
through a QML Image and its other theme loading behind the shown one — and the dark filter per
pixel; shapes with the mouse, a stylus and fingers, the lasso and moving, the selection's actions,
text and labels, erasing shapes, painting shapes and text light and dark, Qt SVG drawing them, a
merge with shapes, text drawn in a worker thread, 600 shapes; plugin files loaded, saved in their
format, a stroke erased after a save marked deleted, a text's link kept when moved and its edit
kept in later saves, another device's file merged into theirs, the oldest form and a frame read-only, a `.excalidraw` saved as one; the previews of plugin files and
the plugin's export standing in),
`tst_drawing_qml` (the route and its notices, the background menu, a drawing of shapes with the
shapes' menu and everything selected, a frame read-only, embeds and the picture view in the real
app, Obsidian Excalidraw plugin drawings — editable, a frame, the oldest form, opened as a page,
embedded three ways —, a drawing on a PDF's slide with its page picker and the paper's menu,
light and dark; nothing written by looking), `tst_editor_picturesize`
(`drawingFiles`, `drawingSizeFollowsTheFile`, `drawingExportsStandIn`), `tst_editor_blocktext`
(`drawingEmbeds`), `tst_editor_controller`
(`drawingSlashCommand`, `drawingEmbedsFollowTheirFile`, `drawingOnSlashCommand`, `insertDrawingsOnPdfPages`,
`insertDrawingOnPicture`, `insertDrawingsOnOddNames`, `insertDrawingsOnPreparedFiles`, `importBackdropSource`,
`discardBackdropSource`), `tst_drawing_backdrop` (the backdrop element: set,
replaced, removed, read back, only index 0, hostile markers and files, page ranges, the SVG's theme
with a backdrop, the merge, a new drawing on a backdrop, the random stamp), `tst_pdfpages` (this
build's PDF renderer on a PDF written with QPdfWriter; broken files; hand-made pages far larger than
A4 and thinner than a pixel; `pdfRenderDpi`), `tst_documentpaths` (Android document URIs that name
their file), `tst_drawingcanvas` (`backdropUnderInk`, `backdropIsLocked`,
`backdropDarkInverted`, `backdropPreviewDark`, `setRemoveBackdropUndo`, `replacedBackdropsAreNotKept`,
`backdropMerge`, `svgPictureShowsBackdrop`, `prepareBackdropPicture`, `prepareBackdropPdfPage` — and
the page thumbnails —, `backdropsJobs`, `saveTimeWithBackdrop`), `tst_workspace` (`importAttachment`,
`importedCopyDiscarded`, `importAndroidDocument`), the
Android bridge self-test (a PDF page through PdfRenderer, the first page's size), `tst_shell_workspace_pictures`
(`drawingsAreSavedAndShownAgain`, `excalidrawMarkdownIsADrawing`), the shell's self-test (`tst_shell_qml`, `_capture`: opened from
its embed, drawn with a finger / the mouse, saved, undone; the embed in the app's other theme;
the embed of a drawing whose file names another frame (excalidraw.com's margins) keeping its size
when loaded and while the app's theme switches;
a rectangle dragged out (R / the shape button), selected with a tap, deleted from the selection's
bar and undone, Ctrl+A and Escape, text written with the text tool and its keys given back (the
desktop run) — with a pause longer than the save timer mid-word: the editor stays open and keeps
the keys —; on the phone every button of the tool bar on screen; a version written behind the vault's back merged by the save; one with a frame not
written over, the strokes kept in a copy), `tests/app/drawing/svg-theme-check.mjs` (a browser — Chrome over CDP —
shows `tests/data/drawing/auto_theme.svg` dark when it prefers dark, light otherwise, and inside
an `<img>` in the page's own colour scheme — an app's dark theme on a light system; CI) and
`tests/app/drawing/wasm-drawing-check.mjs` (the web build: a drawing's embed light and dark;
`--text`: text typed on the drawing in the browser reaches its file) and
`tests/web/stylus.spec.mjs` (Playwright, GitHub Actions: the web build's editor under a pen in
Chromium — DevTools' trusted pen with pressure: the stroke keeps it, the line grows thicker on
screen, one constant pressure and the mouse have none, the barrel button erases, the file keeps
the pressures — and in Chromium and Firefox with pen events made by the page — out-of-range
pressures clamped, the eraser end erases; the app reports its canvas: `?selftest=pen`,
src/app/diag/webselftest.h, `tst_webselftest`), `scripts/android-emulator-test.sh --stylus` (the
Android emulator: a stylus with pressure injected as the shell user — `adb input` presses with 1.0
only —, the eraser end's tool type, the barrel button, a finger after a stylus moving the paper) and
`tests/app/drawing/excalidraw-roundtrip.sh` (online, no ctest: a diagram drawn with the editor's
tools — `tst_drawingcanvas shapesSample` — dropped on the real excalidraw.com, which must keep
every element as written (`excalidraw-roundtrip.mjs`); its scene as excalidraw.com stores it opened
editable in Brulion, edited, written and taken to excalidraw.com again).
Screenshots: `tests/app/drawing/screenshots.sh` (the `theme` shot: the background's menu;
`shapes`, `selection`, `shapemenu`: a diagram, it all selected with its bar, the shapes' menu).

## Scrolling performance

Long pages, the journal stream, Tasks and the calendar agenda are lazy `ListView`s: a row is
created when it scrolls into the list's cache and destroyed when it leaves, on the GUI thread,
while the list moves. Measured with `--scroll-probe` (see "Dev tools"); ctest
`tst_scroll_jumps_qml` fails on jumps (also of the scroll bar's thumb), `tst_touch_flick` /
`tst_touch_flick_compressed` on Qt's touch faults, `tst_row_scroll` on the scroll bar.

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
7. **A long list whose rows differ in height uses `BRowScrollBar { list: … }`**, not the attached
   `BScrollBar` (its thumb jumps: "The scroll bar: `RowScroll`" below). A model that hides rows as
   zero-height delegates offers `visibleCount`, `visibleRowsBefore()` and `visibleRowAt()`.
8. **Measure, before and after:** `tests/app/perf/scroll-probe.sh` (also `CACHE=0`: every row
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
  `EmbedView`, `FrontmatterView` (folded, and expanded: `PropertiesPanel` sums its rows,
  `PropertyListValue` lays out its chips like `TasksRow`; a journal day's editor binds
  `propertiesExpanded` to the setting itself — a `Binding` element applies only once the day is
  complete, after its editor made its rows, and a day the stream created folded opened a moment
  later, 240–556 px; `tst_scroll_jumps_qml` scrolls days with properties, one case with the panel
  expanded), `PlanningItemRow`, `CalendarAgendaList`, `TasksRow`
  (its chips' line breaks), `TableBlockView`'s buttons and `BChip` itself — Tasks and the agenda
  reuse their rows (`reuseItems`), and a reused row whose chips took their new widths a polish
  later wrapped onto another line after it was placed. (In Qt 6.11 a Repeater inside a delegate
  creates its items at once — `incubateObject` gives the delegate's context no incubator — but
  the bindings do not rely on that.)
* a query block runs its first query when its `QueryResultModel` is completed: on the guide page
  its results (773 px) used to arrive after the editor had placed the block;
* pictures: the `embeds` role has each picture's `width` × `height` (0 × 0 for a format the device
  cannot decode: not read) — a raster picture's header
  (`QImageReader::size()`), an SVG's root `width` / `height` / `viewBox` given to Qt's own SVG
  reader as a document of their own (so units, percentages and the viewBox follow Qt's rules
  exactly), the whole SVG only when the root leaves the size to the drawing and the file is at most
  64 KB (else unknown: sized when loaded); cached per file, modification time and size. In the
  browser the size comes with the picture's `data:` URL (`EditorHost::assetPixelSize`). When the
  loaded picture has another shape than its header said (Qt's pixmap cache still had a picture
  since replaced in the file), the frame takes the picture's shape — never stretched;
* the zoom breadcrumb above the blocks is hidden with its Loader (an inactive Loader kept its
  height: a blank band after zooming out);
* a zoom that begins or ends while a block is edited (Enter in a zoomed heading or paragraph,
  undo / redo across it, the breadcrumb) keeps the edited block in view: `BlockEditor`
  `onZoomChanged` lays the list out first (the rows the zoom showed or hid get their heights),
  then positions it at the block and the caret; positioned before that layout, the view stayed
  among the zoom's rows and then showed the page's top (`EditorPlayground` self-test: a heading
  and a list item far down a long page, the breadcrumb's arrow clicked while a child of a folded
  zoom root is edited — the fold opens, the child keeps the keyboard; `tst_content_journals_qml`:
  a zoomed paragraph at the bottom of a long day, the stream scrolled by `ensureEditVisible`);
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

### The scroll bar: `RowScroll` (`src/app/ui/rowscroll.h`)

Qt's attached scroll bar (`T.ScrollBar.vertical: BScrollBar {}`) takes its position and size
from Flickable's `visibleArea` — `contentY`, `originY` and `contentHeight` — and ListView
estimates those from the mean height of the rows it has created at the moment. Blocks and days
differ a lot in height, so as rows come and go while the list scrolls the estimate changes and
the thumb jumps, back and forth, although the content moves smoothly (ListView keeps it in
place). ListView takes no per-row size hints, and a cache of measured heights would still move
the thumb the first time each row is measured (every new day of a first pass through the
journal) and go stale with every width change.

`BRowScrollBar { list: list }` (`qml/components/BRowScrollBar.qml`) is a `BScrollBar` whose
geometry comes from `RowScroll` (library `brulion_ui_scroll`, Qt Quick's public API only):

* **Progress in rows.** t = above / (above + below): `above` the rows above the view's top edge
  plus the fraction of the delegate at the edge, `below` the same under the bottom edge; the
  header and the footer count as one row each; exactly 0 / 1 at `atYBeginning` / `atYEnd`. An
  edge in a gap between rows (`spacing`, a section header — HistoryView, LogsView) is the
  boundary before the next row, whether or not the row on the gap's other side is created
  (ListView releases a row once its end left the view and the cache: an edge in a section
  header often has no row above it), so the thumb stands still while a gap passes the edge.
  Creating rows off screen or ListView moving `originY` changes neither the row at an edge nor
  where in it the edge is, so the thumb moves only with the content, or when the row count
  changes (a fold, a new block, a new day at midnight). Its speed follows rows, not pixels: a
  tall day takes longer to cross than a short one.
* **Hidden rows are no rows.** A model that hides rows as zero-height delegates says which it
  shows — `visibleCount`, `visibleRowsBefore(row)`, `visibleRowAt(n)` (the editor's
  `BlockListModel`: folded blocks, a zoom) — and then counts only those: crossing a fold of 100
  blocks is not a jump of 100 rows.
* **Size from ListView's estimate, frozen while the list moves** (`moving`) or the bar is held,
  so the thumb never "breathes" while scrolling; it is updated when the list stands still, where
  the bar has faded out. position = t · (1 − size) keeps the thumb in the track and reaching both
  ends exactly.
* **Dragging** inverts it: the row for t is put at the view's top edge (`positionViewAtIndex`,
  then the fraction through `contentY`; twice, with the visible row count at the new place), at
  most once a frame (`RowScroll.ScrollIntervalMs`): dragging over a year of the journal creates
  the days on the way like "Przejdź do dnia" does, not at every mouse move. While the bar is held
  its position is not pushed back; on the release it settles where the list is.
* Recomputed once a frame after the list's polish (`QQuickWindow::afterAnimating`; a 0 ms timer
  without frames): a few `indexAt` / `itemAtIndex` calls. Without a row at an edge (an empty list,
  the middle of a relayout) it falls back to Qt's estimate (`estimated`).
* The bar is not attached (an attached bar's position and size are overwritten from
  `visibleArea`): it places itself over the list's right edge between its margins, as a child of the list (never
  of its scrolling content item; `tst_scroll_jumps_qml` checks it with `--probe-row-bar`), shows while the list moves (`BScrollBar.listMoving`;
  hover and press as before), and passes the wheel on to the list (a ScrollBar takes no wheel
  events). Touch UIs: an indicator, as `BScrollBar`.

Used by the block editor's list (not with `autoHeight`: the journal's days have none), the
journal stream, Tasks, the calendar agenda, the event log and the version history; short or
uniform lists keep `BScrollBar`. ctest `tst_row_scroll`: the row arithmetic (data-driven: header,
end, boundaries from both sides, sections, a row taller than the view, missing rows, NaN), and a
live ListView of 400 rows of 18–600 px with its cache off — the progress moves only in the sweep's
direction and by at most the rows crossed, while `visibleArea` jumps (the canary: when it stops
jumping, Qt changed), the size stays during a flick, dragging and the wheel over the bar, rows
inserted above, a fold with and without the model's help. The scroll probe checks the thumb too
("row scroll bar thumb jumps", `THUMB JUMP` lines; `tst_scroll_jumps_qml` fails on them) next
to the jumps of `visibleArea` it reports; `SelectionSelfTest` drags the editor's thumb (nothing is
selected) and turns the wheel over it.

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
* **Creating a day of the journal is one step**: ~10 ms plus ~1 ms per block on a laptop, 7–9 ms
  per block on a Raspberry Pi 4 (a day of 27–42 blocks: 0.25–0.28 s; with a query block in it
  more). See "a day of the journal stream is created whole" above for why it is not spread over
  frames.
* **A picture from the internet fetched for the first time** has no known size: with
  "Pokazuj obrazy z internetu bez pytania" on, its frame (320 × 180, or its placeholder's height)
  takes the picture's size when the fetch ends, so a row can change height while the journal only
  scrolls (an exception to rule 2). Later visits get the size from the cached file's header like
  an attachment; keeping the placeholder until the picture is scrolled to, or anchoring the view
  on a row above that changed, is not done.
* **Creating a table or a query block is expensive on ARM**: one or two frames of 60–100 ms each
  on a Raspberry Pi 4 (20–30 ms on a laptop), so a slow phone drag over the guide page is not
  smoother there than before (frames over 50 ms per run 1–7 → 8–9, the longest ~100 ms in both);
  the query block's results used to arrive a frame later and push the content down instead.
* The probe's "view velocity after" a release is still Qt's damped value (Flickable updates
  `verticalVelocity` with the restarted flick's first animation step); its "start velocity from
  its distance" shows what the fling did, and its TouchFlick line counts corrections and caught
  flings. In observe mode it judges touch drags only (a mouse or a stylus moves the content
  through Qt's mouse path).

## Page properties

The page's properties — the YAML frontmatter and the leading `key:: value` lines (docs/SYNTAX.md
§4 "Editing page properties") — are edited as typed fields in the editor's **properties panel**
(*Właściwości*) and, for tags and aliases, in the page header; nobody has to touch the raw text
(it stays one menu item away: "Edytuj jako tekst"). Each change is one undo step of the page
(Ctrl+Z, a toast's "Cofnij") and rewrites only the lines of that property — comments, order,
quoting and line endings of the rest stay as they were (`markdown/pageprops.h`, every YAML edit
verified by parsing the result). The page's properties are the YAML block and the first block of
`key::` lines, as the index sees them: more `key::` blocks after a blank line are shown as property
rows but are the page's text, not in the panel. An edit reads the whole text again: a lone fence
being typed ("```js" in the edited block) is closed first, as the save writes it (also before a
note mode switch or an inserted template), so it never runs on over the page.

**The panel** (`qml/editor/FrontmatterView.qml` → `PropertiesPanel.qml` → `PropertyRow.qml`,
`PropertyListValue.qml`, `PropertyMenu.qml`, all for the leading frontmatter row's delegate):

* Folded (the default: the journal stream stays dense): "Właściwości", the count and a one-line
  preview of the values ("status: w toku · koszt: 1200"). The header's tap switches it; whether
  it is open is a device setting (`AppSettings.propertiesExpanded`, key
  `editor/propertiesExpanded`; `BlockEditor.propertiesExpanded` bound by `PageView` and the
  journal days, `propertiesExpandedToggled` writes it), so a page stays open while it is edited,
  undone or synced. Expanded, the header has "+" (add, Ctrl+;) and "⋯" ("Edytuj jako tekst": the
  raw text in the block's inline editor, "Zwiń").
* A row: the type icon (Text `type`, Number `sigma`, Checkbox `square-check-big`, Date `calendar`,
  Date and time `calendar-clock`, List `list`, Tags `tags`, Aliases `bookmark`, other `code`), the
  key and the value's editor by type. Text is shown rendered (`[[links]]`, `#tags`, URLs work —
  the `properties` role's `html`, escaped by the `InlineRenderer`); a click beside a link opens a
  `BSuggestField` with the raw text (the notebook's values of the key as suggestions, after `[[`
  pages); a text of several lines gets a text area (Ctrl+Enter or leaving it commits). Numbers
  take "3,5" and "1 200" (an invalid one: the field's error, nothing written). A checkbox writes
  each toggle. Dates open the editor's date dialog (`EditorDialogs.pickPropertyDate`: the key as
  title, natural language "jutro", the clock for a date and time, "Wyczyść"). Lists, tags and
  aliases are chips (× removes; a `[[Page]]` item and a tag open their page) with "+": a field
  where Enter or "," adds (a pasted "a, b, c" adds three) and stays open, Backspace in the empty
  field takes the last chip, Ctrl+Z there undoes; what cannot be added (an alias with `|`, a tag
  that is none) is said under the field, and in a toast when the field is left. Fields commit on
  Enter and when they lose the focus (also when the row goes away, and before the editor shows
  another page: `PageController.pathAboutToChange`), Escape leaves them unchanged. A field starts
  with the value of the moment it opened — not a binding: the notebook changing (sync, another
  pane, the page's own save) re-renders the rows and must not replace what is typed — and writes
  only what differs from it, so a value opened and left as it was keeps its form (a `|` text,
  `ilosc:: 1.50`).
* The row's menu (its type icon, a right-click or a long press on the row, the Menu key /
  Shift+F10): "Typ" (Tekst, Liczba, Pole wyboru, Data, Data i godzina, Lista; tags and aliases keep
  theirs — a value that cannot be converted is cleared with "Cofnij" in the toast; an empty value's
  type is remembered for the session, `BlockEditor.propertyTypeOverride`, a list is written `[]`),
  "Zmień nazwę" (the key becomes a field with key suggestions; an existing name, `brulion` and a
  key a `key::` line cannot hold are refused with a sentence under the field), "Kopiuj wartość",
  "Usuń" ("Cofnij" in the toast); a row the fields cannot edit (a map, a nested list, a block
  Brulion cannot edit safely) offers "Edytuj jako tekst" instead.
* "+ Dodaj właściwość": a field with the notebook's keys (key, its type's icon, "12 stron";
  `PageController.suggestPropertyKeys`, without the page's own keys and `brulion`). Choosing or
  entering a key writes it at once with an empty value of its type in the notebook (`key:`,
  `key: []`, `key: false`; `PageController.addPageProperty`) and puts the keyboard on its value;
  a key the page has already (in either block, in any case) is refused under the field — adding
  never overwrites a value; Escape drops it, nothing written.
  The type of an empty value is the key's most common type in the notebook
  (`PageController.notebookPropertyType`, from `VaultIndex::propertyKeys()`).
* A block Brulion cannot edit safely (an indented mapping, plain text between `---` lines) shows
  its values read-only: "Tych właściwości nie da się bezpiecznie zmieniać w polach." with
  "Edytuj jako tekst".
* Layout: the type icon and the key in a column (as wide as the widest key, 96 px to 30 % of the
  panel; keys at the UI text size) beside the value — on phones too, rows 44 px on touch UIs (32 px,
  28 dense, with a mouse); only a panel whose value column would be narrower than ~168 px (a narrow
  side panel) stacks the key over the value. Every control and tap area is a 44 px target on touch
  UIs: the key's whole cell (a tap focuses the row, a double tap renames), the value's whole cell
  (a text, a number, a date button: the row's padding included). The open panel is a sunken surface
  without a frame (the focus border while its text is edited). Heights are bindings (rule 1 of
  "Scrolling performance": the rows' heights summed, the chips' lines computed), rows live in a
  `ListModel` by key so an edit keeps them (a focused field too).
* Keyboard: rows take the focus (Tab); ↓ / ↑ move between them (past the last: the add row), → or
  Enter edits the value, ← or F2 renames, Delete or Ctrl+Backspace removes, Alt+↓ goes to the first
  block, Ctrl+; adds, Ctrl+Z / Ctrl+Shift+Z undo and redo, Escape returns to the editor. A field
  with the keyboard ends block editing and is kept above the on-screen keyboard
  (`BlockEditor.editingProperty` in `ensureEditVisible()`); the touch toolbar stays away.

**Adding the first property** (`BlockEditor.addProperty()`: the page menu's "Dodaj właściwość",
Ctrl+;, the palette's "Dodaj właściwość strony" — uiAction `addProperty`, handled by `PageView` and
for the journal stream's active day by `JournalsView` —, the phone menu): zoomed in, the page is
zoomed out first; when the block new properties go to is shown (`pageProperties.addRow`), that
panel opens at its add row (`propertyFocus` { part: "add", source }); otherwise — a page without
properties, or one whose frontmatter holds only the hidden note mode, also when `key::` lines are
shown below it — a draft add row comes above its blocks (`propertyDraft`, a `PropertiesPanel` with
`draft` in the list header). The first key writes a new frontmatter (or into the hidden block, or a `key::`
line on a page that has only those) and the real panel takes over, its value focused
(`propertyFocus` survives the new row's creation).

**The page header** (`ContentPageHeader`): the aliases and tags come from
`PageController.pageProperties` once the page is loaded (an edit shows at once), else from the
index's summary. A right-click or a long press on a chip from a property opens its menu — a tag:
"Otwórz stronę tagu", "Usuń z tej strony"; an alias (also a tap): "Kopiuj", "Usuń alias"
(`removePageListItem`: from every property holding it; the last one takes the property away;
"Cofnij" in the toast). The row ends with "+" (when properties can be added): "Dodaj tag", "Dodaj
alias" (a field at the end of the row — tags suggested —, Enter adds and stays open, Escape
closes; leaving it adds what it holds, a refusal said in a toast) and "Dodaj właściwość…". The
header is the editor list's header, so in source mode (Ctrl+E: the list is hidden) neither the
chips nor "+" are shown — the source text is edited directly there. While this page's properties
panel is shown and open (`BlockEditor.propertiesExpanded`, `pageProperties.visibleRow` ≥ 0) the
header's row is not shown either: the panel right below it has the same chips with the same actions
(a tag opens its page, × removes, "+" on the tags and aliases rows adds, the panel's "+" adds a
property, the row menu copies a value); folded, the header's row is back.

**C++** (`PageController`, `src/app/editor/pagecontroller_properties.cpp`): `pageProperties`
{ items, tags, aliases, canAdd, addTarget, hasYaml, hasLogseq, editable, problem, visibleRow,
addRow } (read from the leading frontmatter rows, again only when their text changed; in source
mode from the source text), `setPageProperty`, `addPageProperty`, `removePageProperty`,
`renamePageProperty`,
`setPagePropertyType` ({ lost } when the value could not be kept), `addPageListItem`,
`removePageListItem` — each one undo step through `applyText` (in source mode an edit of the
source text, like `setMode`), returning { ok, status, reason } —, `suggestPropertyKeys`,
`suggestPropertyValues`, `notebookPropertyType` over `EditorHost::propertyKeys()` /
`propertyValues()` (`Workspace` and `FakeEditorHost`: `VaultIndex`); the signal
`pathAboutToChange` (before `setPath` saves and reads another page). `BlockListModel`: the
`properties` role is typed and cached per row (`pagePropertyMap`: key, value, type, source, raw,
items, empty, editable, multiline, linkDate, html, itemsHtml), `propertiesInfo` { source, editable,
problem, canAdd }; `replaceContent` keeps a leading frontmatter row whose text changed (same index,
same form) as the same row (the same uid) with a `dataChanged` — the panel's delegate survives
property edits, the note mode and other devices' changes; undo and redo keep every changed block's
row anyway.

**Tests**: `tst_markdown_pageprops` (types, reading, the writing rules byte for byte, random
input), `tst_index_properties`, `tst_editor_pageprops` (undo steps, the row kept, source mode,
refusals, suggestions, the roles), QML `tst_editor_qml_properties` / `_touch` (real input, see
"Dev tools"), the shell self-test (the header's chips and the palette command).

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
| Enter | a new paragraph after it (also after a heading) | a new list item (checkboxes and numbering continue); at the start of the text: an empty item above it (a task: an empty task; the text keeps its status and children); on an empty item: leave the list — the item becomes a paragraph, or moves one level out when nested |
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
on pages), a marker column of `listIndent` per list level (the same small round bullet at every
depth — nesting shows in the indent —, numbers from the `listMarker` role, task checkboxes in
place of the bullet),
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
* **Settings → Notatnik, "Ustawienia z Obsidiana"** (only while the notebook has no
  `.brulion/config.json` and settings were taken from `.obsidian/`, `Vault::configAdoptedFromObsidian`
  → `Workspace.configFromObsidian()`): the row above "Folder dziennika" lists the settings taken
  (their row titles: Folder dziennika, Nazwy plików dziennika, Szablon dziennika, Folder szablonów,
  Folder załączników) and says that changing any setting saves them to `.brulion/config.json`;
  **Zapisz w notatniku** (`Workspace.saveAdoptedConfig()`) writes them at once (for the browser
  with Nextcloud, which never sees `.obsidian/`). Problems with Obsidian's files (a date format
  without a Qt equivalent, attachments "next to the note") show in the warnings card at the end
  of the section, like those of config.json.
* The **journal stream** shows each day in its own resolved mode (journals default to outline).
  A day switched to a document among outline days keeps their left edge (the date, the text:
  `BlockEditor.documentInset` = the outline's gutter); with `journalMode` document the days use
  the full width.

## Sync visibility and the event log

The user must always be able to tell what the sync is doing, whether something failed and
why — and be able to send us the log (docs/ARCHITECTURE.md "Logging").

* **The Nextcloud account and the notebooks** (`qml/io/NextcloudForm.qml` over
  `NextcloudSetup`, in the sync center and Settings → Synchronizacja): the server, the user and
  the app password are shared, the switch and the server folder belong to the open notebook (no
  notebook open: nothing syncs). A notebook without a folder of its own is offered "Notatki"
  for the first account on the device, else its own name ("Praca", "Praca 2" when taken —
  never a folder another notebook has, holds or is inside); one synced before keeps the folder
  its sync bookkeeping names. A folder another notebook on the device syncs with (its folder
  exists, its switch is on) is an error under the field at once ("Notatnik „Dom” synchronizuje
  się już z tym folderem…") and is not saved while sync is on. Two notebooks that 0.3.1–0.3.5
  left on one folder (or one inside the other) sync nothing while that lasts — no automatic
  sync and no "Synchronizuj teraz" (it only says why: one click would mix the two notebooks for
  good, and choosing another folder is one step away): the status ("Synchronizacja wstrzymana:
  notatnik „Dom” na tym urządzeniu synchronizuje się z tym samym folderem…") stays in the sync
  footer and the sync center (red, error code `folder-shared`), the event log says it once per
  notebook opened and folder, a toast too, and the overview's Nextcloud card offers „Wybierz
  inny folder” (the Nextcloud tab, whose form shows the error under the folder). "Wyloguj"
  switches sync off in every notebook (the dialog says so), saving another server or user in all
  the others. The switch and folder of 0.3.0 and older go to the notebook they belong to
  (AppSettings).
* **Live progress** (`qml/io/SyncLiveProgress.qml` in the Nextcloud tab's status card, from
  `Workspace.sync.live`, at most four updates a second and once a second for the elapsed time;
  no timer while idle): the phase in words ("Sprawdzanie plików na serwerze…", "Wysyłanie i
  pobieranie zmian"), "folder 12 z ok. 40" / "sprawdzono 120 z 3125 plików · zmiany: 3", a bar
  (indeterminate until the files are known), "Trwa 1 min 20 s · 1,4 MB · po zmianie notatki ·
  #42", the files in flight ("Teraz: …") and "Zatrzymaj". When nothing arrives for 20 s an amber
  box says "Serwer nie odpowiada od 25 s…" with what Brulion waits for; after 180 s the run is
  stopped and retried — the panel never says "syncing" forever. `Workspace.sync.statusText`
  (sidebar footer, settings) follows the phases at the same pace.
* **Conflict copies of drawings** (`SyncConflictResolver.qml` over `ConflictResolver`, list
  entries `drawing`: the copy or the original is a drawing — an SVG with an Excalidraw scene,
  `*.excalidraw.md`, `.excalidraw` —, a pen icon in the list): never the text merge (a line merge
  would cut the scene's payload). A card "Dwie wersje rysunku" with read-only previews
  (`DrawingCanvas { interactive: false }`: the whole drawing in view, centred both ways, no
  input, the app's theme;
  a drawing Brulion does not draw as its picture, `Workspace.drawingUrl`) of this device's
  version, the copy and — when both are drawings Brulion edits, of one format — "Po scaleniu",
  the elements of each ("3 elementy") and "wspólne: 2, tylko tutaj: 1, tylko w kopii: 1"; on
  phones two previews in a row and the merge below, in a scrolling area. **Scal rysunki**
  writes the union (core `drawing::merge` with no common version: Syncthing keeps none — the
  original's elements, order, paper and Markdown around the scene, plus the copy's other
  elements on top; for an element both have the higher version, a tie the original's) into the
  drawing (`WorkspaceSync.resolveConflictBytes`: its previous version in History, unthrottled)
  and moves the copy to the trash. Elements erased on one device only come back (the card says
  so). Same elements (another program wrote the picture) → "Kopia ma ten sam rysunek"; the copy
  contained in the drawing → "Wszystko z kopii jest już w rysunku" ("Usuń kopię"). A drawing
  Brulion cannot merge (a frame, a picture without a scene, a broken one) gets only "Zachowaj
  moją" / "Zachowaj kopię" (the copy's exact bytes, the replaced version in History). An
  Obsidian plugin file (`*.excalidraw.md`) is a note too: its Markdown around the scene
  (frontmatter, the back of the card, element links, embedded files, text entries) counts
  (`drawing::keepsMarkdownOf`: the copy's must survive in the original's — text entries follow
  their elements). When the copy's notes would be lost, nothing is "the same" or "older": "Scal
  rysunki" (the card says so) merges the elements into the drawing and **keeps the copy**
  (`ConflictResolver.updated`, a toast, the resolver stays open); then — or when the copy adds
  nothing to the drawing at all, a merge would leave it as it is (the copy's own paper, an element
  edited by hand: never taken) — a card "Różnią się tylko notatki wokół rysunku" with a
  `BDiffView` of both files' Markdown (the scene's block left empty, `oursNotes`/`theirsNotes`)
  and "Usuń kopię" once the user carried over what they need in Obsidian. The replaced version
  goes to History whatever its size (`writeBytesKeepingHistory` records it from the disk: the
  vault snapshots only the content it holds in memory, up to 16 MB; nothing is written when it
  cannot be recorded). A decision made on versions that changed meanwhile is refused and shown
  again. Desktop: "Otwórz rysunek" in the header. Tests: `tst_appio_conflicts`;
  `tests/app/io/syncthing-conflict.sh` (no ctest, needs `syncthing`: two Syncthing instances on
  127.0.0.1 change an SVG and an Obsidian drawing while paused, their own conflict copies merged by
  `realSyncthingConflicts` on both devices' folders). Nextcloud's own sync merges a drawing changed on both devices by itself, against
  the version both had last (`drawing::mergeDrawingFiles`); only drawings it cannot merge so
  become such copies.
* **Retry and decisions:** a failed run shows "Kolejna próba za 25 s" (a countdown) with
  "Spróbuj teraz"; held-back deletions and conflicts are "Czeka na Twoją decyzję" / "Konflikt w
  … — scal wersje" (the deletion guard and the conflict list stay as before). Moving or renaming
  folders and pages never asks: content that arrives under a new path in the same run is a move,
  not a deletion (`SyncEngine`, docs/ARCHITECTURE.md "Sync guards").
* **Folders** (Nextcloud): empty folders travel both ways, an emptied folder deleted on one side
  goes on the other, a folder or page renamed in the app is moved on the server (one MOVE), and
  one renamed on the server is renamed here too. The status card, the history row ("1
  przeniesiony folder", "2 przeniesione pliki", "2 nowe foldery") and the run's details
  ("Foldery: 2 nowe, 1 usunięty, 1 przeniesiony na serwerze, 1 przeniesiony tutaj";
  `WorkspaceSync.folderCountsText`, and "Pliki: 3 przeniesione na serwerze, 1 przeniesiony
  tutaj"; `WorkspaceSync.fileMovesText`) name them; folders a run created or removed here reach the
  tree at once (`WorkspaceSync.foldersChanged` -> `Workspace.notifyFolderChange`: folders make no
  Vault change of their own), a folder the run renamed here as the server had renamed it follows
  in the tree and the routes (`WorkspaceSync.folderMovedHere` -> `Workspace.folderRenamed`), and
  a folder the user creates, deletes or renames starts a sync after the debounce like an edit
  (`WorkspaceSync.notifyLocalChange`).
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

`BButton`, `BIconButton`, `BTextField`, `BSuggestField` (a field with a suggestion list: key and value suggestions of the properties panel), `BTextArea`, `BSearchField`, `BSwitch`, `BCheckBox`,
`BChip`, `BBadge`, `BTabBar`/`BTabButton`, `BSegmented`, `BBreadcrumb`, `BCard`, `BListItem`,
`BSectionHeader`, `BKbd`, `BText`, `BDialog` (bottom sheet on narrow windows),
`BConfirmDialog`, `BMenu`/`BMenuItem`/`BMenuSeparator`, `BToast` (+ `BToastHost`; the message is
plain text),
`BToolTip`, `BEmptyState`, `BProgressBar`, `BSpinner`, `BScrollBar`, `BRowScrollBar` (long
lists with rows of different heights: the thumb from the rows at the view's edges, `RowScroll`),
`BSplitHandle`,
`BShadow`, `BFocusRing`, `BDatePicker` (month grid, Polish names, natural-language input with
live preview; its clock dialog is created on first use), `BTimePicker`/`BTimePickerDialog` (a
round 24-hour clock: tap or drag hours and minutes — the face takes every press, so a drag may
start on a number or the chosen value's bubble —, keyboard arrows; the focus ring shows for the
keyboard only; in a window too short for it, a phone held sideways, the dial shrinks and the
hours / minutes fields go beside it; the number circles are sized from the rings' geometry — the
inner ring's twelve hours sit closer together, so they are sized first and the outer ones fit the
room left between the rings — so none overlap, down to the dialog's 180 px dial and with larger
text; the outer numbers keep the small text size, the inner ones get a smaller font only where two
digits would not fit), `BReminderDialog` (date/time, alarm switch, recurrence picker,
quick picks: za 10 min, za godzinę, dziś 19:00, jutro 8:00, poniedziałek 9:00),
`BColorSwatch` (a round colour button: selection ring and check mark, tooltip),
`BAccentDialog` (a custom accent: colour grid, `#rrggbb` field, live preview of what
`ThemePalette` makes of it), `BFontPickerDialog` (installed font families with fuzzy search, each
row in its own font, the default first), `BDiffView`,
`BMergeView` (per-conflict choice: Moja / Ich / Obie / Ręcznie), `BPalette` with `BCommandPalette` and
`BQuickSwitcher`, `BKeyboardFocusKeeper` (the focused form field above the on-screen keyboard,
"On-screen keyboard"), `BKeyboardPlaceholder` (a simulated keyboard's grey block). Icons: SVGs in
`assets/icons/` (Lucide, ISC licence) via `BIcon`.

## Strings

`qsTr()` everywhere with English source text; Polish translation in `i18n/brulion_pl.ts`
(default UI language; `brulion_en.ts` only holds English plurals). Plurals with
`qsTr("%n notes", "", n)`. The language can be switched at run time (settings, palette).
Qt's own texts (dialog buttons, `FilePicker`'s fallback widget file dialog) come from Qt's
catalogs `qtbase_pl.qm` / `qtdeclarative_pl.qm`, bundled as resources (`QtTranslations` in
`src/app/ui`; nix: `qt6.qttranslations`) and switched with the language. The system's own file
pickers (Android, Windows, GTK / KDE, the portal, the browser's) speak the system's language.

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
  `accent` is for lines, dots and focus marks (3.7:1 with white). The surfaces (`bg*`,
  `border*`), the accent family and the text colours come from **`ThemePalette`**
  (`src/app/ui/themepalette.h`, library `brulion_ui_helpers`, QML singleton):
  `Theme.colors = ThemePalette.colors(dark, lightSurface, darkSurface, accentName,
  customAccent)`; `accentBg` is the accent at 12 % / 18 %. `tst_ui_themepalette` checks every
  preset and custom extremes on every surface (and that the defaults are exactly the old
  literals); `danger*` never follows the accent (`Theme.marginRed` where a red mark is meant).
  Inputs set by the shell (Main.qml binds them to AppSettings; `--accent`, `--surface`,
  `--density` win): `mode` ("system" | "light" | "dark"), `reducedMotion`, `editorFont`,
  `fontScale`, `editorFontSize`, `accentName` + `customAccent`, `lightSurface`, `darkSurface`,
  `uiFontFamily`, `editorFontFamily`, `monoFontFamily` (checked with `DeviceInfo.hasFontFamily`),
  `lineSpacing` (→ `lineHeightLoose`), `density` (→ `dense`: control and row heights,
  `listItemHeightDense`, `sidebarWidth`), and **the main window must bind its size**:
  `Binding { target: Theme; property: "windowWidth"; value: window.width }` (same for
  `windowHeight`) — `narrow` (< 900 px), `compact` (< 600 px) and `touchUi` derive from it.
* **Components** are built on `QtQuick.Templates` (no style dependency; the few files that need
  a ready control import `QtQuick.Controls.Basic`, never `QtQuick.Controls` — its run-time style
  selection packages all six styles, and configure refuses it, "File pickers"), each file documents
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
  Fields for a date or a recurrence rule in words (`BDatePicker`'s natural-language field, the
  recurrence dialog's rule) set `Qt.ImhNoAutoUppercase`: Gboard otherwise capitalised the first
  letter ("Za 2 minuty"). Block text keeps the keyboard's sentence capitalisation.
  A binding that has to follow a value it does not otherwise use — a counter
  (`Workspace.revision`, `calendar.revision`, a Repeater's `revision` that its itemAdded /
  itemRemoved bump: itemAt() notifies nothing), what a method call's result depends on
  (`DatesHelper.locale` and `today`, a text item's `width` and `contentHeight` for
  `positionToRectangle()`) — reads it for real: as an argument
  (`DatesHelper.weekdayNames("short", DatesHelper.locale)`) or in the result
  (`if (Workspace.revision < 0) return …`, `revision >= 0 && …`). qmlcachegen compiles a bare
  `void Workspace.revision` — any statement that only reads a value — away, and the binding's
  dependency with it, in Debug and Release builds alike (a renamed page's tag strip, a picture's
  text, a created daily note's top bar actions came only with some later change; a query's results
  replaced by as many taller rows overlapped what follows, a date picker kept yesterday's today
  marker, the reminder dialog its weekday names after a language change, a calculation's results
  stayed put when a narrower window wrapped the text again).
  `scripts/check-qml-reads.sh` finds such statements (ctest `tst_scripts_check_qml_reads`, CI's
  lint job); the playground's `refreshtest` checks the views (`tst_editor_qml_refresh`, `_touch`).
* **C++ helpers** (QML singletons): `DatesHelper` (natural-language dates, human formatting,
  recurrence rules, localised names, month grids — all values are strings, no JS `Date`),
  `TextHelper` (diacritic folding, fuzzy ranking for palettes, escaping), `DiffHelper` (Myers
  line diff, word-level highlights, conflict hunks), `KeyboardInsets` (the on-screen keyboard's
  overlap of the window, "On-screen keyboard"), `GestureInsets` (the system's gesture zones at
  the window's edges, "Window layout"; `tests/app/tst_gestureinsets.cpp`), `DeviceInfo`,
  `DevTools`. The first five
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
  New icons: copy from `lucide-static`, normalise like the others, re-run CMake. They render
  everywhere, the browser included (the SVG plugin is linked statically there; `BIcon` recolours
  on the CPU, no shader effect).
* **Emoji and symbol fonts** (`src/app/ui/fallbackfonts.h`, see below).
* **`FilePicker`** — the system's file picker (`src/app/ui/filepicker.h`, see below).

### File pickers: `FilePicker` (`src/app/ui/filepicker.h`)

Every file or folder the user picks goes through the system's own picker; the app never imports
`QtQuick.Dialogs` (its non-native fallback dialog exists once per style and pulls Material,
Universal, Imagine, Fusion and FluentWinUI3 into every package — docs/PERFORMANCE.md L9).

```qml
FilePicker {
    id: saveDialog
    objectName: "exportSaveDialog"
    fileMode: FilePicker.SaveFile          // OpenFile, OpenFiles, OpenFolder, SaveFile
    title: qsTr("Save the export")
    nameFilters: [qsTr("ZIP archives (*.zip)"), qsTr("All files (*)")]
    defaultSuffix: "zip"                   // SaveFile: "Notatka" → "Notatka.zip"
    currentFolder: exporter.defaultSaveFolder
    dark: Theme.dark                       // the fallback dialog's colours:
    colors: Theme.colors                   //   the mode and Brulion's palette
    onAccepted: exporter.saveTo(selectedFile)   // selectedFiles, selectedFolder
}
// saveDialog.selectedFile = suggestion; saveDialog.open()   — asynchronous, never exec()
```

* **Per platform**: the platform theme's dialog helper first — the code path QtQuick.Dialogs
  took, so nothing changed there: Android's Storage Access Framework (`ACTION_OPEN_DOCUMENT`
  with several files, `ACTION_OPEN_DOCUMENT_TREE`, `ACTION_CREATE_DOCUMENT` with the suggested
  name as `EXTRA_TITLE`; content:// URLs with persistable permissions), Windows' `IFileDialog`,
  the GTK / KDE dialog or the XDG portal on Linux where the platform theme has one (the AppImage
  asks for the portal itself: `QT_QPA_PLATFORMTHEME=xdgdesktopportal` unless the user set one;
  docs/NIXOS.md). Elsewhere (a Linux desktop without such a theme, offscreen runs) Qt Widgets'
  `QFileDialog` — desktop builds have Qt Widgets for the tray anyway — in the Fusion style (set on
  the dialog and on every widget in it, so a desktop style such as Breeze or Kvantum cannot draw
  its own colours) with Brulion's colours from `colors` (Theme.colors) and `dark`
  (`widgetDialogPalette()` in `filepicker_p.h`: window = bg, fields and lists = bgElevated, text =
  fg, placeholders and disabled text = fgFaint, the selection = accentSolid under white text; a
  missing or malformed colour is ThemePalette's default of the mode). Android and the browser
  have no Qt Widgets. The event log names the picker each `open()` showed (`brulion.platform`:
  "File picker: QGtk3FileDialogHelper", "QXdgDesktopPortalFileDialog", "QFileDialog", …).
* **Browser**: `available` is false; the controllers show the browser's own picker
  (`ImportController.pickInBrowser()`, `PageController.pickImageInBrowser()`: several files at
  once, files above the desktop's limits skipped before they are read, and the reading stopped
  at the file that would take the pick above its total — the import's 1.5 GB as on the desktop,
  512 MB of pictures —, since the browser holds every read file in memory; `setPickedFiles()` /
  `pictureReceiver()` turn the picked files into the input or the embeds on every platform) and
  save with the
  browser's save picker or a download (`ExportController.saveInBrowser()`, `Logs.download()`) —
  `src/platform/wasm/wasmfiles.h`, docs/WEB.md.
* **API**: QtQuick.Dialogs' names (`fileMode`, `selectedFile(s)`, `selectedFolder`,
  `currentFolder`, `nameFilters`, `defaultSuffix`, `title`, `visible`, `open()`, `reject()`,
  `accepted` / `rejected`), so `ioselftest` still watches `visible` and `selectedFile`. `open()`
  while open does nothing (Android has one picker); a picker cancelled, closed by `reject()` or
  accepted with nothing chosen (some portals) is `rejected`; the object destroyed while its
  picker is up closes it and drops a late answer. `selectedFile` set before `open()` is the save
  picker's suggestion. The window it is modal to: that of the nearest `Item` it belongs to.
* **Rules**: new QML imports `QtQuick.Templates as T` or `QtQuick.Controls.Basic`, never
  `QtQuick.Controls` or `QtQuick.Dialogs` — `src/app/CMakeLists.txt` stops configuring, the APK
  build (`scripts/android-build.sh`, `nix/android.nix`) and the browser build
  (`scripts/wasm-build.sh`) fail when another style or QtQuick.Dialogs is packaged.
* **Tests**: `tests/app/filepicker` (fake backends: what each mode asks for, the signals, open
  while open, destroyed while open; the widget dialog's palette from Brulion's colours, malformed
  ones too; the widget dialog offscreen picked for real, Fusion in light and dark colours),
  `tests/platform/tst_pickedfiles.cpp` (the browser pick's bookkeeping), the import self-test's
  export save dialog (`tst_appio_import_qml*`), the browser pick's results on every platform
  (`tst_appio_import` `browserPickAsGathered`: the same files, warnings and name as the desktop's
  gatherer; `tst_editor_controller` `pictureReceiver`: skipped and empty pictures, the block moved
  or deleted while the picker is open, one undo step), `scripts/wasm-smoke.mjs --pick-files` (the
  browser's `<input>` with seven files — one too large, the last two past the total — and a
  download, CI) and
  `scripts/android-emulator-test.sh --pickers` (DocumentsUI on an emulator).

### Emoji and symbol fonts

Brulion writes emoji into notes and shows them in the UI: ⏰ reminders, 🔔 alarms in rendered
blocks, the task fields 📅 ⏳ 🛫 ✅ ❌ 🔁 🔺 ⏫ 🔼 🔽 ⏬ ➕ 🆔 ⛔ 🏁, ⧉ before block embeds,
setting descriptions and hints ("⏰ jutro 9:00"). Text never draws them itself: Qt takes each
character the text's font lacks from a fallback font, and where no font has it, it draws the
font's missing-glyph box.

**The browser bug** ("white empty squares instead of the bells"): Qt for WebAssembly has no
system fonts. It brings exactly two, DejaVu Sans and DejaVu Sans Mono
(`qwasmfontdatabase.cpp`; fonts of the computer only with the Local Font Access API, which Qt
asks for only on request and which only Chromium has), and DejaVu has none of Brulion's emoji
— every one of them came out as DejaVu's hollow box, white in the dark theme. ⧉ and some rare
LaTeX symbols were boxes too. The Lucide icons (`BIcon`) were never affected.

**The fix**: two small fonts in `assets/fonts/` (SIL Open Font License 1.1, `OFL.txt`; sources
and SHA-256 in its `README.md`), made by `scripts/fallback-fonts.py` from the fonts of the
nixpkgs that `flake.lock` pins:

* `BrulionEmoji.ttf` ("Brulion Emoji", 162 KB, ~100 KB compressed): Noto Emoji (monochrome,
  instanced at weight 400), every emoji of `src/` plus ~300 common ones (faces, hearts, hands,
  notes and work, time and status, a few places, weather, food, activities; single code
  points — no flags or ZWJ sequences) and the five skin-tone modifiers, which the monochrome
  font draws as the plain hand (👍🏻 → 👍, not 👍 and a box). Monochrome glyphs take the text
  colour, so they look like the rest of the UI.
* `BrulionMath.otf` ("Brulion Math", 9 KB): Noto Sans Math, the non-emoji characters of `src/`
  that DejaVu Sans lacks (⧉ ⌢ ⌣ ⎰ ⎱ ⟮ ⟯ ⨄ ⨆ ⨿ 𝒜).

Both carry their own family names and DejaVu Sans's vertical metrics (Noto Sans Math's are half
as tall again), so a line with a fallback glyph is no taller than one without. `main.cpp`
installs them right after the application object, before any text is laid out
(`installFallbackFonts()`):

* the emoji font as the application emoji family: Qt ≥ 6.9 lays out emoji sequences as runs of
  their own whose fallbacks are the emoji families, colour fonts first;
* both as application fallback families of Common text (Qt files Latin under it, and gives a
  symbol the script of the text around it: "Spotkanie ⏰ jutro" is one Latin run), **behind the
  app's default family**: `[default family, Brulion Emoji, Brulion Math, the system's…]`. In
  the browser the same for every other script: its font database has no fallbacks beyond
  DejaVu Sans, so 🗓 or ⧉ inside Cyrillic or Greek text ("Встреча 🗓") stayed boxes otherwise.
  On Linux fontconfig already tries every font for other scripts, the application fonts
  included, so their order stays fontconfig's;
* first, the families Brulion asks for — the default font, the system fixed font, `serif`
  (`Theme.fontSerif`), `sans-serif` and `monospace` (formula spans) — are pinned to the fonts
  Qt resolves them to (`QFont::insertSubstitution`). Qt gives a family name that is no installed
  family ("serif" in the browser, fontconfig's generic names) the first font of its fallback
  list; without the pin and the default family at its head, the browser's editor text got Brulion
  Math as its font (its line metrics moved every block down a few pixels).

Text keeps its own font; only the characters it lacks come from these (tested per character).
Policies:

* **Browser**: both, always (`FallbackFontPolicy::Always`).
* **Linux** (packages, AppImage): both only when the system's fonts draw some of
  `brulionEmoji()` as boxes (`WhenMissing`: one `QTextLayout` of the probe, ~11 ms cold on the
  reference laptop, most of it loading the system emoji font that the first emoji would load
  anyway); otherwise a colour emoji font of the system (Noto Color Emoji, Twemoji…) keeps
  drawing the emoji, and only Brulion Math becomes an application font (~1 ms, no fallback
  families, no pins): fontconfig's fallback lists include application fonts, so it draws ⧉ and
  the math symbols where no system font has them (DejaVu with a colour emoji font has none). The
  symbols are not looked for first: a character no font has costs ~15 ms of start-up (Qt opens
  fallback font after fallback font). An AppImage on a system without an emoji font thus gets
  monochrome emoji instead of boxes.
* **Android**: none — it always has Noto Color Emoji; its build leaves the fonts out.

The event log notes a font that fixed boxes ("Fallback font Brulion Emoji installed: the system
fonts drew ⏰🔔… as boxes") and warns about boxes left. Tests (`tests/app/fonts`):
`tst_fallback_fonts` — both fonts cover their probes and have DejaVu's metrics; every emoji in
`src/` is in the probe and in the font, so a new emoji without a regenerated font fails; the box
check; installing removes the boxes in the default, serif and fixed fonts while letters keep
their font, and no requested family (nor an unknown one) gets one of these as its font; a hand
with a skin tone is one glyph without a box, and a symbol with U+FE0F or U+FE0E (❤️ ✔️ from
phone keyboards) has none in the default and the fixed font; in the browser every script gets
the fallbacks; `tst_fallback_fonts_when_missing` — the Linux policy;
`tst_fallback_fonts_browser_fonts` and `…_browser_fonts_when_missing` — the same with a
fontconfig that knows only DejaVu Sans and DejaVu Sans Mono, the browser's fonts (there the emoji
are boxes first, and lines with emoji keep their height); `…_system_emoji_when_missing` — DejaVu
and an emoji font: the emoji stay the system's, the symbols are boxes first and drawn by Brulion
Math afterwards. In the browser: `scripts/wasm-smoke.mjs --glyphs` (`?selftest=glyphs`,
docs/WEB.md). A new emoji in the UI or the syntax: add it to `brulionEmoji()`, re-run
`scripts/fallback-fonts.py` (it takes every emoji of `src/` by itself), commit `assets/fonts/`.

## Dev tools, screenshots and UI tests

Command-line options of `brulion` (`src/app/ui/devtools.h`, `src/app/shell/appcontroller.h`,
`src/app/main.cpp`); unknown options are ignored. Scripts and AI agents that read or change notes
use the separate command line `brulion-cli` instead (docs/CLI.md: JSON output, an MCP server). Headless runs:
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
| `--editor [--editor-script <state>]` | the block editor playground (`qml/dev/EditorPlayground.qml`, in-memory `FakeEditorHost`) in a scripted state: `editing`, `suggest`, `tags`, `tag-pages` (the `#` popup offering pages), `slash`, `date`, `zoom`, `source`, `reminder`, `table`, `calc`, `query`, …; the document mode on a sample trip plan: `document`, `document-paragraph`, `document-list`, `document-heading`, `document-task`, `document-outline`; self-tests that exit: `selftest` (ctest `tst_editor_qml`; also names with markup in menus and breadcrumbs, a block put at the start of a page by a real Enter or Alt+Up, undone with real Ctrl+Z and redone with Ctrl+Shift+Z, Enter at the end of a zoomed paragraph ending the zoom with the new block edited and in view, Ctrl+Z zooming back in, Ctrl+Shift+Z out again, the breadcrumb's arrow clicked while a child of a folded zoom root is edited), `blocklinks` — links to blocks and headings with real input: the block menu from right-clicks on a heading's text, a paragraph, a callout, a calc block and a page title drawn above the page's properties (none from the properties), a bullet's right-click / long press and the touch toolbar's "Więcej" (above the bar; a table cell's too), a finger's long press on text selecting the block (no menu), each "Kopiuj odnośnik" item read back from the clipboard with its toast, Ctrl+Shift+C editing / selected / nothing selected / in a table cell, the palette after a cell lost the keyboard, copied links followed (scrolled, selected, flashed — again after scrolling away), a missing heading (ctest `tst_editor_qml_blocklinks`, `_touch`); states `copylink`, `copylink-document` (the menu with its submenu open on a heading), `copylink-toolbar`, `copylink-table` (with `--touch`: the menu from the toolbar's "Więcej" of a block, of a table cell), `clock` — the round clock with real mouse / touch input (numbers that never overlap and stay legible, down to the smallest dial, with larger text too), its dialog, the clock buttons of the date picker and the calendar's add dialog (ctest `tst_editor_qml_clock`, `_phone`, `_landscape`), `taskstatus` — the task status menu with real right-clicks / long presses on the editor's, a query result's and an embed's checkbox (the result's and the embed's menu staying open while their note changes elsewhere, the status written on the task's line now), Ctrl+Shift+Enter and the menu's keys (none on a heading), the "/" status commands typed with real keys, the touch toolbar's task button held down (ctest `tst_editor_qml_taskstatus`, `_touch`), `selectiontest` — selecting and copying blocks with real input (`qml/dev/SelectionSelfTest.qml`): Esc and Shift+↓ then Ctrl+C, Shift+↑ / Shift+↓ from an edited block, Ctrl+A twice, Shift+click, Delete and Ctrl+Z, the toast's Cofnij (clicked; one whose deletion was undone already undoes nothing), Alt+↓ and Tab on a range, Ctrl+V over a selection, in-block Ctrl+C / Ctrl+X keeping no-break spaces, the edited text's right-click menu, the block menu of a selected and another block, the floating tools, a mouse drag over a document's paragraphs (no scrolling) and to the view's edge (autoscroll), the band after scrolling away, the floating tools' Więcej (no indents for a document's paragraphs); with `--touch` a long press, taps, the end grip, Usuń and a tap on the toast's Cofnij, a long press dragged to the edge, Back, the toolbar's and the bullet menu's "Zaznacz" — the clipboard read back (ctest `tst_editor_qml_selection`, `_touch`), states `date-menu`, `date-menu-reminder` (a 📅 / ⏰ chip's menu), `recurrence-menu` (a 🔁 rule's), `link-menu` (a #tag's), `datechips` — the token menus with real input (`qml/dev/DateChipSelfTest.qml`): a right-click / long press on a 📅 and a ⏰ chip (no block menu, nothing selected or toggled), Dzień później, Dziś, Zmień datę… (the dialog on the chip's date, a date and time written), Usuń datę, Godzinę później, Alarm, the reminder dialog, each undone; a plain click / tap on the chip still edits; a priority set lower, a 🔁 rule set to Codziennie, the reminder's Priorytet submenu, a `[[day]]` link a day later (each undone), the link menus of a `#tag` and a `[[link]]` (Kopiuj read back from the clipboard) (ctest `tst_editor_qml_datechips`, `_touch`), `propstest` — the page properties panel with real input (`qml/dev/PropertiesSelfTest.qml`; "Page properties"; the key suggestions following their field when it moves) (ctest `tst_editor_qml_properties`, `_touch`), states `properties`, `properties-phone` (the panel expanded on a page with every type), `refreshtest` — views following what they depend on (`qml/dev/RefreshSelfTest.qml`): a query's results replaced by as many rows of another height, an embedded section changed in its own note, a date picker's today marker after midnight, the reminder dialog's weekday names, rule and date picker after a language change, a calculation's results after the window narrowed until the text wraps again (the width depends on the fonts), the view staying down the page when the page's header changes its height (ctest `tst_editor_qml_refresh`, `_touch`); states `selection` (a range; desktop: the floating tools, `--touch`: the docked tools and grips), `selection-document`, `selection-menu` (the block menu of a selected block), `text-menu` (the edited text's menu), `keyboard` — the touch toolbar docked on a simulated keyboard (with `--touch --keyboard 0`; ctest `tst_editor_qml_keyboard_touch`) |
| `--demo` / `--vault <path>` / `--onboarding` | seed and open the sample notebook (in `--vault`, else "Przykładowy brulion") / open this folder / start on onboarding |
| `--route <view or JSON>` | initial route, e.g. `settings` or `'{"view":"page","path":"Projekty/Remont mieszkania.md","open":"links"}'` |
| `--share <json>` | receive a share as Android's `ShareActivity` hands it over — `{"subject", "text", "url", "files": [{"path", "name", "mime"}]}` (`src/core/notebook/share.h`) — and open the share dialog once the notebook is ready (`AppController::receiveShare`; screenshots, manual checks, ctest `tst_shell_share`) |
| `--share-query <query>` | the browser version's share target: `share-title=…&share-text=…&share-url=…` as `wasm/index.html` passes a share launch's parameters (`notebook::sharedContentFromQuery`, `AppController::receiveShareQuery`); works on every platform (ctest `tst_shell_share`) |
| `--open <file>…` | the files to offer in the share dialog ("Otwórz za pomocą": the desktop entry's `Exec=brulion --open %F`, the Windows installer's `"…\brulion.exe" --open "%1"`): every argument after it is a file — paths relative to the working folder or `file://` URLs —, never an option, the options are the arguments before it (`notebook::splitOpenArguments`); a file of the open notebook opens as its page; copies are saved, the files are never changed; without files a plain start. A second launch sends them to the running Brulion (`share`, ipc/appmessage.h) |
| `--popup <name[:query]>` | open a shell popup once the notebook is open and indexed (at most 3 s later without one): `palette`, `switcher` (the query as if typed, the caret after it), `capture`, `newPage`, `newFolder`, `move[:<path>[><folder>]]` (else the page shown; with `>` that folder chosen), `about`, `drawer` |
| `--editor-script <state>` (with the app) | `qml/dev/PageScript.qml`: the editor on the guide page (a document) in a state (`editing`, `slash`, `reminder`, `source`, `sidepanel`, `doc-paragraph`, `doc-list`, `doc-heading`, `doc-outline`; any page given by `--route`: `edit:<text>[@<caret>]`, `select:<text>`, `select-range:<first>..<last>` — on touch UIs in the selection mode —, `mode-tooltip`, `properties` — the page's properties panel expanded, nothing focused, the view at the top: the `ui` benchmark's screen —, …) |
| `--screenshot <file> [--delay <ms>]` | save the window after loading and quit |
| `--dump-layout <file>` | with `--screenshot`: write what the window shows (texts with colours and fonts, targets, boxes, theme tokens) as JSON for the UI benchmark (`src/app/ui/layoutdump.h`), from the same frame |
| `--size <WxH>`, `--scale <f>`, `--touch`, `--theme light\|dark`, `--locale pl\|en`, `--reduced-motion`, `--now "<date> <time>"` | window size, device pixel ratio, touch UI, theme, language, motion, fixed clock |
| `--accent <name\|#rrggbb>`, `--surface paper\|white\|slate\|black`, `--density compact\|comfortable` | the appearance settings for this run (screenshots; the settings themselves stay): accent preset or custom colour, light or dark background, density |
| `--keyboard <px>` | a simulated on-screen keyboard of that height (`KeyboardInsets.simulated`; 0 = none, and a real one is ignored), drawn as a grey block of keys |
| `--journey <dir>` (`--journey-theme`, `--journey-empty`) | walk the real app like a first-time user with real input events, a screenshot per step (`src/app/shell/shelljourney.h`; desktop builds) |
| `--selftest-shell` | key routing and focus checks of the real shell, and rows with context menus with real input — taps and long presses (`--touch`: the drawer, All pages, tasks, bullets) or left and right clicks; the drawer's edge swipe (`--touch`: a finger from the left edge opens it, to the left closes it, with a simulated gesture-navigation zone (`GestureInsets.simulate`) from beyond it; a vertical scroll and a swipe farther in leave it closed, a tap in the strip reaches the button under it, no strip while blocks are selected or the keyboard is up, so the selection's start grip at the left edge keeps its drag — the drawer opened by its button still closes with a swipe —, nor on the calendar's week and month grids, whose swipe from the left edge — not a fast flick — shows the previous period, and the drawer opened over them closes with a swipe); the task status menu on the checkboxes of Tasks (and its Menu key: the status highlighted, Up to "Otwórz", Enter), a page's linked references, the calendar's agenda, its day panel (on a phone a bottom sheet, left open) and a journal day, and a "/" status command typed into the journal; links to blocks and headings (a link to a block moved to another page opens that page with the block selected, a missing heading is said — from the text and as a route; a heading named with tags quoted as plain text —, the palette's "Kopiuj odwołanie do bloku" copies the edited block's `((id))`, read back from the clipboard, a right-click on a paragraph opens the block menu, the side panel's outline copies a heading's link — the last two on the desktop); tags as page links (a tap / click on a `#tag` in the journal opens its page — not created by looking at it — with the tagged blocks among its references and the tag strip in its header, back returns; in Tasks a click filters, Ctrl+click opens the tag's page, a long press its menu — "Filtruj po tagu", its "Otwórz stronę tagu" opens the page; a row of the tag list opens the page, its button the overview); in Tasks a due date's chip has the date menu, whose "Dzień później" writes the task's note; a picture attachment (its view, its bar on a phone and in a narrow view beside the side panel, no live tooltip outliving its hidden button, the text view read-only and scrolled by a finger or the wheel, its byte limit cutting before a character, a change on disk shown at once, Ctrl+E both ways, its references, a small picture, a name with markup, a format without a decoder, the top bar's Więcej menu there, the same picture embedded in a note); a drawing (a tap / click on its embed opens the drawing editor, a stroke drawn with a finger — `--touch` — or the mouse is saved by itself with its undo kept, Cofnij / Ctrl+Z is saved too, the page's embed shows the drawing saved again; a text being written low on the paper stays on it when the window shrinks — a browser's keyboard); the Markdown source scrolled by a finger, a tap and a long press in it (`--touch`); the default reminder time set on the clock in the settings; the event log page (a logged line arrives without its secret, the search ignores diacritics, an area chip filters, the detailed switch) and a sync run's details from the history ("Pokaż w rejestrze"); folders (the page opened revealed and highlighted in the tree, a row's "⋯" (`--touch`) or right-click menu, "Przenieś do…" moving a page with the dialog, a root folder from the "Strony" header's "+" typed with real keys, an empty folder deleted leaving the tree, with the mouse a page dragged onto a folder and onto the header, a closed folder opening under a held drag and Escape cancelling it, All pages' folder rows and the breadcrumb back); Settings → Wygląd (the logo's colours by default, a tap on an accent swatch recolours the app and is saved, a colour typed in the accent dialog, a sidebar entry unticked leaves the sidebar — the drawer on phones — and stays in the palette, a bottom navigation slot takes a view from its menu and the phone's bar shows it — the view it already has, picked again, stays checked —, the quick note button switched off and on, "Przywróć domyślny wygląd"; Edytor → Czcionka → "Inna…" cancelled leaves the selection where it was and opens the picker again, a missing family names the serif or sans-serif font used instead); the share dialog ("Udostępnij → Brulion": shared text saved to today's journal by a real tap / click on "Zapisz" without a reminder, a shared `.md` saved as a new page titled after it byte for byte and its copy removed, a file opened with Brulion from outside the notebook — the "to save in the notebook" subtitle and the copy hint, saved as a page byte for byte, the file itself left unchanged —, Escape dropping a share) (ctest `tst_shell_qml`, `tst_shell_qml_capture`; desktop builds) |
| `--selftest-import` (with `--onboarding`) | the ways to the import screen (onboarding, empty journal, drawer, Więcej) and an import from a typed path, with real taps / clicks (`src/app/io/ioselftest.h`, ctest `tst_appio_import_qml*`; desktop builds) |
| `--selftest-sounds` (with `--demo`) | Settings → Reminders: the sounds of reminders and alarms (Silent, Ringtone without one → the sound menu, a theme sound, the ringtone back, Play sample), with real taps / clicks (`src/app/notify/notifyselftest.h`, ctest `tst_notify_sounds_qml*`; desktop builds) |
| `--scroll-probe <target>` (`--scroll-script drag\|flick\|wheel\|touchpad\|observe`, `--probe-out <csv>`, `--probe-passes`, `--probe-wait`, `--probe-max`, `--probe-speed`, `--probe-check <px>`, `--probe-row-bar`, `--probe-cache <px>`) | scroll a view with synthetic input (touch drags / flings, mouse wheel, touchpad) to the end and back — or, with `observe`, record real input (a finger, `adb shell input swipe`) for `--probe-max` ms — record every frame (interval, GUI work, contentY / originY / contentHeight, delegates created (once the view has placed them) — with their kind of block — and resized above / in / below the view, the content's movement on screen against the input, touch moves per frame) and print a summary of stutters, jumps and touch strokes (release speed, fling, its start velocity from its distance; TouchFlick's corrections and caught flings), then quit (`--probe-check <px>`: exit code 1 when the content jumps by more than <px> — at an end of the list only layout shifts count — or the thumb of the view's `BRowScrollBar` jumps (against the content, or while it stands still) — with `--probe-row-bar` also when the view has no such bar over it —, ctest `tst_scroll_jumps_qml`; `--probe-cache <px>`: the list's cacheBuffer first, 0 = every row created at the edge of the view); the log also goes to `<csv>.log` (and logcat on Android) (`src/app/ui/scrollprobe.h`; target `auto`, `sidebar`, `name:<objectName>`, `type:<QML type>`). Android: Qt passes the activity's `applicationArguments` string extra only to debuggable APKs (`am start -n io.github.januszbit.brulion/.android.BrulionActivity --es applicationArguments "'--scroll-probe auto --scroll-script observe --probe-out /data/user/0/io.github.januszbit.brulion/files/p.csv'"`). Built unless `BRULION_SCROLL_PROBE=OFF`, the default of the shipped Android and browser builds (Release / MinSizeRel): measure with `BRULION_ANDROID_BUILD_TYPE=RelWithDebInfo` |
| `--hidden`, `--capture`, `--today`, `--no-single-instance` | start in the tray (Windows' start at sign-in; a second launch with it shows nothing) / open quick capture / today's journal (a second launch asks the running instance — over a socket only in the user's private runtime folder: without one every launch is an instance of its own and the event log says why, docs/CLI.md "The running app") / allow a second instance |
| `cli …` (the first argument) | the command line instead of the app (`brulion cli capture "…"`, also `Brulion.AppImage cli …`): `brulion-cli`, before anything of the app — no window, log file or single-instance claim; see docs/CLI.md. A running app also takes `brulion-cli app open <ref>` / `app today` / `app capture` (the `open {json}` message of `src/ipc/appmessage.h`, `AppController::openRoute`) |
| `--remote-control` | AI agents and scripts drive the running app for this run (as the device setting Settings → Application → "Dla programistów" → "Pozwól agentom sterować aplikacją" does; off by default): a request/response socket of its own next to the single-instance one, owner-only — `brulion-cli app state \| navigate \| open \| find \| click \| long-press \| double-click \| drag \| scroll \| type \| key \| command \| capture \| wait \| screenshot \| quit` and the MCP tools of `brulion-cli mcp --app-control` (docs/CLI.md "Remote control"; `src/app/shell/remotecontrol.h`, desktop builds). Input is played on the event loop with the UI driver of the self-test and the journey (`src/app/shell/uidriver.h`), one gesture at a time, answered once the window settled; ctest `tst_remote_control_qml` drives the real app offscreen through it |

Scripts (inside `nix develop`; one GUI instance at a time via a lock; a fixed clock `NOW`;
the app scripts give every run a fresh temporary XDG home, so the user's own Brulion is never
touched; `SIZES`, `THEMES`, `LOCALE`, `OUT_DIR` select what to render; output in
`tests/screenshots/`, git-ignored):

* `scripts/screenshots.sh [sections…]` — gallery sections (`taskstatus`: the task checkboxes and
  their status menu) × {desktop 1280×800, phone 412×915 `--touch`} × {light, dark};
  `SIZES="desktop-tall phone-tall"` captures long sections whole, `SIZES=phone-landscape`
  (915×412 `--touch`) a phone held sideways.
* `tests/app/editor/editor-screenshots.sh` — the editor playground states.
* `tests/app/shell/screenshots.sh` — the real app: onboarding, journals, the guide page and
  its editor states (`page-editing`, `page-slash`, `page-reminder`, `page-doc-paragraph`,
  `page-doc-list`, `page-doc-heading`, `page-doc-outline`, …), the page menu (`page-menu`), the
  renovation project as an outline (`page-project`), a picture attachment (`page-picture`, its
  text view `page-picture-text`, its references in the side panel `page-picture-links`), the
  document mode on a long Obsidian-like
  note (`tests/app/shell/notemode/`: `doc-long`, editing a paragraph / list item / task / heading
  / numbered item, a selected block, the header's mode toggle with its tooltip, the same note as an
  outline: `doc-long-*`) and in the journal stream (`doc-journal`: all days documents,
  `doc-journal-mixed`: one document day among outlines), settings, palette, switcher, new page
  (with the mode choice), capture, drawer, folders (`tree`, `drawer-tree`, `move`, `move-chosen`,
  `newpage-folder`, `newpage-root`, `newfolder`, `allpages`, `allpages-folder`); with a simulated
  keyboard (`--keyboard 300`) the guide being edited (`page-editing-keyboard`), today in the
  journal stream (`journals-editing-keyboard`) and quick capture's sheet (`capture-keyboard`).
* `tests/app/drawing/screenshots.sh` — the drawing editor (`editor`: a page of handwritten notes on
  lined paper, written by `tst_drawingcanvas handwritingSample`), the page embedding it (`page`), its
  picture view with "Edytuj rysunek" (`picture`), an Excalidraw drawing with text (`readonly`),
  a new drawing (`new`) and drawings of Obsidian's Excalidraw plugin (`obsidian` in the editor,
  `obsidianpage` opened as a page, `obsidianframe` with a frame: read-only).
* `tests/app/content/screenshots.sh`, `tests/app/planning/screenshots.sh`,
  `tests/app/io/screenshots.sh` — content views, calendar/tasks/graph, sync center (`sync-conflict-
  drawing`: the conflict of a drawing, previews and "Scal rysunki"; `sync-conflict-drawing-notes`:
  an Obsidian drawing whose copy differs only in its notes) and import/export with prepared
  notebooks; the io script also seeds a sync history and an earlier
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

### UI benchmark (Brulion vs Obsidian)

`tests/app/uibench/` compares Brulion's screens with the real Obsidian's, reproducibly: the same
notebook, window sizes, themes, fonts, clock and language for both, a screenshot and a layout dump
of every screen in both apps, objective metrics computed from them, and a blind judge panel
scoring a fixed rubric. It drives the loop that improves Brulion's look screen by screen
(Obsidian's binaries and screenshots never enter the repository: they live in
`/home/nixos/workspace/brulion-work/uibench/round-NN/` on the development machine and, in the
GitHub fallback, in a 3-day artifact of a throwaway branch).

Files: `screens.json` (the screens: Brulion's command-line arguments, Obsidian's steps, the
judges' context; checked by `brulion-uimetrics check-manifest`), `bench.sh` (runs everything),
`prepare-vault.sh` (the notebook), `fonts.sh` (the pinned fonts), `obsidian-shots.mjs` (the
Obsidian driver), `brulion-uimetrics` (`uimetrics.h`: the metrics), `judge.mjs` (packs blind
batches, scores the judges' votes), `rubric.md` (the judges' instructions), `lint.sh` +
`lint-allow.json` (the regression guard), `vault/` (the overlay notes), `obsidian/` (Obsidian's
vault settings, the Calendar plugin's hashes).

**Fair conditions.**

| Condition | Brulion | Obsidian |
|---|---|---|
| Version | the working tree (git revision in the manifest) | the official 1.14.4 AppImage, sha256 checked (`appimage-run -w` on NixOS, `--appimage-extract` elsewhere) |
| Notebook | a fresh copy of `vault-brulion` per shot | a copy without `.brulion/`, with `.obsidian/` (daily notes in `journals/`, the Calendar plugin 1.5.10) |
| Sizes | desktop 1280×800 at 1×, phone 412×915 at 2× (`--size`, `--scale`, `--touch`) | the same viewport and scale through `Emulation.setDeviceMetricsOverride`; phone: `EmulateMobile` (the mobile layout, `body.is-phone`) |
| Themes | `--theme light\|dark` | the setting "Schemat kolorów" set to Jasny / Ciemny (`app.vault.setConfig('theme')` + `app.updateTheme`; a "Use light/dark mode" command when the version has one, never the toggle) |
| Language | `--locale pl` | Polish (`localStorage.language`, else the setting and a restart; English is flagged) |
| Clock | `--now "2026-10-14 10:00"`, `TZ=Europe/Warsaw` | a running `Date` shifted to that instant (`Page.addScriptToEvaluateOnNewDocument`) and the zone override |
| Fonts | `FONTCONFIG_FILE=<out>/env/fonts.conf`: Noto Sans / Serif / Sans Mono, Noto Color Emoji, DejaVu (nixpkgs), grayscale antialiasing, slight hinting | the same file, `--disable-lcd-text` |
| Rendering | offscreen + software (the realism check `--render xcb-gl`: Xvfb + OpenGL/llvmpipe) | `--disable-gpu`, `--force-color-profile=srgb`, on Xvfb's display (`--ozone-platform=x11`) without the caller's desktop session (no Wayland, session bus or keyring: `--password-store=basic`), a DevTools port of its own (read back from the profile: another Obsidian on the machine is never driven) |
| Settings | defaults (fresh XDG home) | defaults (fresh `--user-data-dir`, `HOME`, `XDG_*`): Live Preview, readable line length; the settings screen in the main window (1.14 opens them in a window of their own: one 1280×800 picture like Brulion's) |
| Pointer | none (offscreen) | desktop: moved outside the page before each shot (no hover highlight); phone: touch emulation (no pointer) |
| State between screens | a fresh process and home per shot | one session per size; before each screen the session's starting state comes back: the phone's drawers closed by Obsidian itself (opening one slides the navigation bar aside, only closing slides it back), the workspace layout, `localStorage` (the explorer's unfolded folders, recent searches) and the explorer's folders — checked, and a screen whose light and dark shots show different texts is flagged (`light-dark` warning) |
| Network | none needed | a dead proxy (`--proxy-server=http://127.0.0.1:9`), `updateDisabled` |

**The notebook** (`prepare-vault.sh`): Brulion's sample notebook seeded at the bench's clock
(`--demo --vault`), plus the overlay `vault/`: `Wyprawa w Bieszczady.md` (a long document in plain
Obsidian-compatible Markdown: headings, a callout, lists, tasks, a table of plain numbers, a quote,
code, a picture; links to `[[Przeprowadzka]]` twice), `Projekty/Przeprowadzka.md` (an outline
of list items only, three levels, tasks, links, tags) and `Projekty/Kuchnia.md` (front matter
with a property of every type both apps edit as fields). The notes' sha256 go into the manifest:
both apps start from byte-identical notes. A pristine notebook of another overlay or clock is
never reused.

**The screens** (24 groups × light and dark = 48 pairs): `daily`, `doc`, `doc-edit` (desktop),
`outline`, `tree` (desktop), `drawer` (phone), `backlinks` (desktop), `search` (`remont`),
`switcher` (`rem`), `palette`, `settings` (appearance), `graph`, `calendar` (Obsidian with the
Calendar plugin; without it the pair is n/a) and `properties`. Not compared: Tasks, All pages,
Tags, sync, import (no Obsidian equivalent — no free wins).

**Running it.** On the laptop (the data directory outside the rsynced tree):
```
scripts/remote-build.sh -n uibench brulion brulion-uimetrics \
  --run 'BRULION_GIT_REV=<rev> BRULION_GIT_DIRTY=false tests/app/uibench/bench.sh --out $HOME/work/brulion-uibench-data/round-00'
rsync -a laptop:work/brulion-uibench-data/round-00/ /home/nixos/workspace/brulion-work/uibench/round-00/
```
`--app brulion --reuse-obsidian <round-00>/shots/obsidian` re-shoots only Brulion (Obsidian is shot
once per bench configuration: the manifest's `benchConfigHash` changes with screens.json — Brulion's
arguments aside —, the notes, the overlay, the fonts, the Obsidian driver, the clock or Obsidian's
version, and the reuse is refused when the source round's hash differs); `--only`, `--sizes`,
`--themes` narrow it (into an earlier round's folder: only those shots are replaced, the metrics
are measured again from every picture there); `--render
xcb-gl` is the realism check (doc and daily on the desktop under Xvfb with OpenGL, into
`shots/brulion-gl/` and `metrics-gl.json`); `--dry-run` prints the commands (ctest
`tst_uibench_bench_dryrun`). Output: `shots/<app>/<id>-<size>-<theme>.{png,layout.json}`,
`metrics/<app>/*.json`, `metrics.json`, `manifest.json` (versions, fonts, the notes' hashes,
Obsidian's environment and warnings, failures), `logs/`. The tools (Xvfb, fc-match, Node 22,
appimage-run) come from the repository's nixpkgs; the AppImage and the Calendar plugin are checked
against their hashes before use and fetched again when they are not whole (without the plugin the
calendar pairs are n/a, without Obsidian only Brulion is shot). The manifest's `dirty` is `null`
(unknown) for a tree without `.git` unless `BRULION_GIT_DIRTY` says it. When the laptop is off: the workflow
`ui-bench.yml` lives only on a throwaway branch `ui-bench` (never merged, artifact kept 3 days),
`gh run download -n ui-bench`.

**Layout dumps** (schema 1, `src/app/ui/layoutdump.h`): every visible text (painted rectangle,
colour, opacity, font size, weight and family, lines, rich-text colour fragments with their
backgrounds), every target (buttons, mouse areas, tap handlers, fields — a mouse area filling its
button counts once), every filled box, and the theme's tokens, in window coordinates; a text or
target is `occluded` under a later opaque box or a popup's dim. Obsidian's dump comes from the DOM
in the same schema (text nodes grouped by their nearest non-inline element, so a paragraph with
links and tags is one text with fragments, as in Brulion's rich text; form fields' values — a
property's key and value, a typed query, a select's choice — are texts as Brulion's fields are).
The note's text itself is `inline` in both, as WCAG 2.5.8 treats targets in running text: Brulion's
tap areas over a rendered block's text, Obsidian's CodeMirror content (a tap puts the caret into the
line under it in both); a list property's empty field in Obsidian is its whole box.

**Metrics** (`brulion-uimetrics measure`, each 0–10; `summarize` → `metrics.json`):

| | Metric | Weight |
|---|---|---|
| C | text contrast: WCAG ratio of every enabled text (placeholders too; emoji skipped) on the background sampled from the PNG, the minimum over its fragments; 10 × Σ chars·min(1, ratio / required) / Σ chars (required 4.5, large text 3) | 0.25 |
| T | target size: desktop WCAG 2.5.8 (24 × 24, or a 24-px circle clear of neighbours — a target containing it does not count), phone 44 × 44 (Material's 48 reported) | 0.20 |
| Y | type scale: sizes with ≥ 2 % of the characters (6 or fewer, 3 or more, none within 8 % of another), at most 3 weights | 0.15 |
| A | alignment: share of left edges shared by ≥ 3 items (0.6) and of rows whose centres line up within 1.5 px (0.4) | 0.15 |
| S | spacing: share of insets below 64 px (from the smallest box around; larger ones are positions, not padding) and gaps between texts on a 4-px grid (±1) | 0.10 |
| K | token contrast: the theme's text colours on its page and sunken surfaces | 0.10 |
| L | line length: share of body text in lines of 45–90 characters (desktop doc, doc-edit, daily, outline, backlinks) | 0.05 |

O is their weighted mean (renormalised without L or K); in a pair (`metrics.json` `pairs`) both
apps' O use only the metrics both shots have (the shot's own O stays as `Oshot`). Each measurement also lists the failing
texts and targets (30 worst) for the critic, and report-only extras (edge density, distinct
colours, non-text contrast of targets).

**Judging** (`judge.mjs`, `rubric.md`): a group is a screen at a size with its light and dark
pair; `pack --round DIR --judges 3 --batch 4 --seed N` copies the images into blind batches
(`judging/batches/bNN/gNN-<theme>-A.png`, A/B drawn per judge, group and theme, balanced over the
panel; the key in `judging-keys/key.json`), `--only-changed PREV` packs only groups whose shots
changed (either app's); the others keep the votes of the round that judged them last, through any
number of rounds. `pack` refuses a round that has votes already (`--force` discards them). A vote
file `judging/votes/bNN.json` belongs to the batch its name says (a `batch` field naming another
one drops it); a re-judged batch saved as `bNN-<anything>.json` replaces those votes. Every judge scores both images of each pair 1–10 on eight anchored criteria
(hierarchy, typography, spacing, colour, consistency, calm, affordance, appeal), with a strength
and a weakness each, and guesses which one is Obsidian only after scoring. `score --round DIR
[--prev PREV]` → `scores.json`, `report.md`: per pair J = the judges' median of the criteria's
mean, O from the metrics, **P = 0.75·J + 0.25·O**, margin = P_Brulion − P_Obsidian, agreement,
the overall means with a 95 % bootstrap interval, per-criterion means, the panel's blindness.

**The loop and when it stops**: a baseline round, then at most 8 rounds of critic → changes →
re-shoot → re-measure → re-judge; a round is kept when the overall P of Brulion rose by ≥ 0.05
(or O rose and J fell by at most 0.1) and no pair's P fell by more than 0.5, else reverted and
logged as rejected. Brulion wins when every valid pair has a margin ≥ 0.2, J at least Obsidian's
and 2 of 3 judges agreeing, and overall the margin is ≥ 0.5 with the interval above 0, J higher
and O not lower — confirmed by fresh judges; the loop also stops after 3 rounds without +0.05.
Accessibility first: no change may lower C or T, remove a feature, or shrink touch targets below 44
px.

**Regression guard** `tst_uibench_lint_qml` (`lint.sh`, slow): 8 Brulion shots (daily, doc,
settings, palette × desktop light / phone dark; system fonts) fail on any enabled, visible text
below its WCAG contrast or a target below its size that `lint-allow.json` does not excuse
(`[{screen, device, kind, type, text|name, count, reason}]`: `count` excuses that many failures of
a shot of that device, so one more is caught). Its entries were the round-0 baseline of Brulion's
known failures (the bullet's zoom handle, the journal's day titles, its 12-px band under a day and
the settings' 38-px segmented choices on phones, the calculation block's faint caption); round 1
fixed them but one: a parent's bullet beside its fold arrow on phones (28 × 44 — both cannot be
44 px wide in the outline's gutter).

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
| `telefon-edycja-ciemny.png` | shell `page-editing-keyboard` | phone, dark |
| `foldery-przenoszenie.png` | shell `move` | desktop, light |
| `telefon-zaznaczenie.png` | shell `page-selection` | phone, light |
