# Brulion — UI (Qt Quick / QML)

One QML UI for desktop (wide window, keyboard + mouse), Android (phone, touch, soft keyboard)
and the browser (Qt for WebAssembly). Layout adapts to the window width, not the platform.
Qt Quick Controls with the **Basic** style, themed by our own `Theme` singleton and wrapped
components — no Material/Fusion look, so all targets look identical.

## Look & feel

* **Graphite, the default look** (since the redesign after 0.27.0, modelled on chat apps the
  person likes — Discord, FluffyChat, Instagram — rather than an IDE): **layers of grey instead of
  black and hairlines** — the rail darkest (`bgSunken` darkened), side panes `#16171b`, the page
  `#1e1f24`, cards, menus and dialogs `#27292f`, popups over them `#30323a`; the panes part by
  their shade, so the split handles and the rail draw no line there (`Theme.paneLines` false) —,
  **soft shapes** (`Theme.radius` 10 for controls, `radiusLg` 14 cards and menus, `radiusXl` 22
  dialogs, `radiusSheet` 28 sheets, capsules for chips, search fields, toggles and the phone's
  navigation), **the accent on the one primary button** of a view (`Theme.buttonPrimary*`: the
  accent's solid fill under `fgOnAccent`; ink stays for selected discs and chips), a pressed button
  shrinking a little (`Theme.pressScale` 0.96, springing back), the notebook as a round mark
  (`BAvatar`: its first letter on the accent; a rounded square under the pointer, like Discord's
  servers). Cool text (`#d3d8e2`, muted `#a9b0bf`), links `#8fb8ea`, tags violet `#c89bf0`, results
  and "synced" green `#7fd1a8`, and a **coral accent** `#ff7a66` whose solid fill carries dark ink
  (`#2b0a05`, `Theme.fgOnAccent`). **"Nocny"** — dark, dense, true OLED black (`#000`; side panes
  `#08090b`, cards `#0d0f13`, hairlines `#1a1d23` / `#262a32`) — is the "Black" background.
  **"Atrament"** (the "Ink" background, from the "Papier i atrament" design) is graphite's warm
  sibling: warm charcoal layers (side panes `#151412`, page `#1e1d1a`, cards `#282723`, popups
  `#312f2b`), parted by shade like graphite (`Theme.layered`), with warm paper-white text
  (`#ede8de`, muted `#bdb6a8`, faint `#9a9385`), links `#93bceb`, tags `#c9a2f0`, results `#84d0a6`.
  Its light counterpart: the paper surfaces with coral darkened to `#c4422f` (white text on its
  fill). **Reminders and alarms (⏰) are amber whatever the accent** (dark `#f2b33d` on amber at
  13 %, light `#8a5900` on amber at 22 % — a shade darker than the mockup's `#9a6400`, which
  reaches only 3.9:1 on the tinted chip over a well), so they never clash with the coral accent
  or the red of overdue tasks; destructive actions stay the margin red `#e2574c`. Brand colours
  of the logo (`assets/logo.svg`): ink `#1f2430`, paper `#fbf7ee`, margin red `#e2574c`, amber
  `#f2b33d`, grid blue `#9fc3e6` (focus, selection). Text contrast ≥ WCAG AA. `Theme.dark` follows
  `Qt.styleHints.colorScheme` unless the user overrides it in the settings (a first start is
  dark).
* **Appearance settings** (Settings → **Appearance**, device-local in `AppSettings`; a phone and a
  desktop want different looks): the theme mode; the **accent colour** — presets Coral (default),
  Red (the margin red, the default before "Nocny"), Blue, Green, Teal, Purple, Graphite, or
  **Custom…** (any
  `#rrggbb` from `BAccentDialog`: a 12 × 3 colour grid, a hex field, a live preview);
  **Interface size** (desktop windows only, `AppController.interfaceScaleSupported`; device-local
  `AppSettings.interfaceScale`, 0 = "Auto · n %"): the whole window drawn larger or smaller — text,
  controls, icons and every size the QML writes as a number — through Qt's global scale factor
  (`src/app/ui/uiscale.h`, `QHighDpiScaling`, multiplied with the system's own scaling), changed
  live: the open windows keep their size in pixels and are laid out again. "Auto" picks by the
  height of the screen Brulion starts on, in device-independent pixels: 1 up to 1300 px (laptops,
  1080p and 1200p monitors), 1.15 (1440p), 1.25 (1600p), 1.35 (1800p), 1.5 (2160p: 4K at 100 %),
  1.75 beyond — a 4K monitor the system already scales by 200 % stays at 1. The choices 90–175 %,
  and the palette's "Larger interface" (Ctrl+Alt+=), "Smaller interface" (Ctrl+Alt+-) and
  "Interface size: automatic" (Ctrl+Alt+0) on the steps 80, 90, 100, 110, 115, 125, 135, 150, 175,
  200 % (a toast says the size; Ctrl+= and Ctrl+- zoom PDFs, drawings and canvases). Not with
  `--scale` / `QT_SCALE_FACTOR`, `--size`, screenshots or a headless platform;
  `ThemePalette` (C++) derives readable shades by itself — the accent as a mark ≥ 3:1, as text
  ≥ 4.5:1 on every surface and on its own tint, the solid fill ≥ 4.5:1 under its text (white;
  dark ink on the dark mode's coral, `accentOn`). The
  accent marks things (today, the current tab, the selected row's bar, drop targets); it never
  means "destructive": danger callouts, danger cards, deleting stay red. The **light
  background** Paper (default) | White (neutral white, grey wells) and the **dark background**
  Graphite (default: layers of grey) | Ink (warm layers, "Atrament") | Blue-grey | Black (OLED:
  true black, barely lifted side panes and cards, "Nocny"); both rows show while
  the mode is "System". **Text size** (UI factor), the **interface font** (any installed family,
  `BFontPickerDialog`; "" = the system's), **density** Comfortable | Compact (desktop only: 24/30/36
  px controls, 28 px rows, 26 px sidebar rows, a 248 px sidebar, a smaller paragraph gap; touch
  UIs keep 44 px targets and never show the row), reduce motion; at the card's end "Restore the
  default look…", asked first (no undo): the accent colour, both backgrounds, text size, interface
  font and density, and the editor's fonts (text, code), font size and line spacing go back to
  their defaults (`AppSettings.resetAppearance()`); the theme, the layout and the text width
  stay. **Layout**: the sidebar's
  entries (Journal, Calendar, Tasks, All pages, Tags, Graph, Search) and its sections
  Favorites and Recent can be unticked (the "Pages" tree stays; hidden views stay in the
  command palette and their shortcuts; ticked, Favorites shows once a page is starred and
  Recent once another page was opened); the phone's **bottom navigation**: five buttons, each
  from a menu of Journal, Calendar, Search, Tasks, Habits, All pages, Tags, Graph (a view
  already in another slot swaps places with it); the top bar's **+** on phones (the quick note
  button) on or off. A family chosen on another device or since uninstalled falls back to the
  default font. The browser has no font rows (Qt there sees only its bundled DejaVu fonts).
* **Themes** (Settings → Appearance → **Themes**, `qml/shell/ShellThemePages.qml`; C++
  `ThemePages`, `src/app/shell/themepages.h`; core `extensions/themepage.h`): the notebook's
  **theme pages** — pages tagged `#theme` / `#motyw` whose ```` ```theme ```` block declares tokens
  (docs/SYNTAX.md §13 "Theme pages": colours for both modes or one, the radii, the spacing unit
  `space`, the fonts) — listed by title next to **None** (the appearance settings alone), each with
  its description (or how many tokens it sets, or "n line(s) ignored"), its background, accent and
  text in the light and the dark mode as two small swatches over the settings in force, a button
  that opens the page and a check mark on the one in force (`themePage_none`, `themePage_<n>`,
  `themePageOpen_<n>`). A click applies it at once, in both modes: its tokens are the theme's bottom
  layer (`AppScripting::setPageTheme`), under an extension's `b.app.theme` and an agent's live
  theme, over the palette of the appearance settings (an `accent` brings its readable shades, as a
  custom accent does). The choice is the **notebook's** — `themePage` in `.brulion/config.json`, so
  every device that follows the notebook shows it after a sync — unless **Only on this device**
  (`themePagesDeviceOnly`) is on: then this device keeps its own (`AppSettings`
  "appearance/themePageOwn/<notebook id>", "appearance/themePage/<notebook id>"; switched on it
  keeps what shows, switched off the notebook's applies again). A page in force applies again when
  it changes (an edit's save, a sync); renamed or moved in the app it stays picked (the notebook's
  setting follows it as `journalTemplate` does; the device's here); a choice whose page is not in
  the notebook (deleted, not synced yet) applies nothing and a warning line says so
  (`themePagesMissing`). **Save the look as a theme page** (`themePagesExport`) writes the look in
  force — the palette of the accent and backgrounds in both modes, the chosen interface and code
  fonts, every override in force — into a new page "Mój motyw" / "My theme" in the folder
  "Motywy" / "Themes" and opens it: a theme is shared by copying that file. On a theme page the
  page itself shows a muted chip at its bottom left (`pageThemeState`, as an extension page's
  chip; none on an extension page): "Theme: in use" or "Theme: not in use" with **Use it**, how
  many tokens it sets — or how many lines it ignored, the first one's reason and a button to its
  line (`pageThemeLine`) —, "no ```theme block yet", and a button to these settings.
* Typography: **bundled fonts** on every platform (`src/app/ui/uifonts.h`, `assets/fonts/ui`,
  made by `scripts/ui-fonts.py`; SIL OFL 1.1, no Reserved Font Names): **Inter** for the UI
  (Regular, Medium, SemiBold, Bold, Italic, Bold Italic) and, by default, note text (the editor's
  "Sans-serif"), **Literata** for the editor's "Serif" (Regular, Italic, Bold, Bold Italic), **JetBrains Mono** for code, dates and meta text (the
  status bar's counts). Static instances subset to the Latin alphabets of Europe and the Greek
  letters with punctuation, arrows and common symbols (other scripts and emoji come from the
  system's fonts or "Brulion Emoji"), no hinting, stylistic sets or default ligatures and contextual
  alternates (`liga`, `clig`, `calt`: their glyphs have no character, and Qt's PDF writer maps
  glyphs back to text through the cmap — Inter's em dash vanished from PDFs; `tst_ui_fonts` checks
  that shaped text uses only glyphs with a character) — 1.4 MB for all twelve —,
  renamed "Brulion Inter", "Brulion Literata", "Brulion JetBrains Mono" so a complete system font
  of the same name never mixes with a subset; the font pickers leave them out and offer them as
  the default row ("Inter (default)", …). Loaded in `main.cpp` before anything is laid out;
  `DeviceInfo.defaultUiFamily` / `defaultSerifFamily` / `defaultMonoFamily` give them, or the
  system's fonts when one failed to load. A chosen interface family (or none: Inter), the editor
  font sans (Inter, the default), serif (Literata) or any installed family (Settings → Editor → Font →
  **Other…**), the code font any monospaced family (JetBrains Mono by default: code blocks, source
  mode, formulas; inline `code` in rendered text keeps Qt's own fixed font); calculations are note text: the editor font at the editor's size, results with tabular
  figures (`tnum`) so they still line up right-aligned, errors smaller in the UI font; line spacing Tight 1.45 | Normal 1.65 | Relaxed 1.9; configurable max
  text width — Narrow 600, **Normal 680** (the default, `Theme.measure`: about 85 characters of
  16-px serif, Obsidian's comfortable measure), Wide 960, Full; the presets before 0.6.0
  (640, 760) read as 600 and 680, other stored widths stay (the calendar's Agenda keeps 760,
  `Theme.measureList`: its rows hold a time, a title and a page) — and text size. One type scale: note text quoted outside the editor (linked
  references, tagged blocks under a page and in the side panel, `ContentRefRow`) is in the
  editor font at the UI's base size (`textBase`), so the editor's own text is the only larger
  size; search results show the page title (semibold, `fg`) and the snippet (`fgMuted`) both in
  the UI font at `textBase`, told apart by weight and colour.
* Inline tokens in rendered text are rounded shapes behind the words, not Qt's square text
  backgrounds: `#tags` and the date / reminder / done / recurrence chips are pills (radius =
  height / 2, the shape of the page header's tag chips, `BChip`), inline `code` and
  `==highlights==` boxes with `Theme.radiusXs`; their colours, tints and the no-break space
  inside each end are the renderer's ("The block editor"). The same in the lists that quote note
  text — linked references, Tasks, embeds, tables, query results — and in link previews
  (`BRichText`, "The block editor").
* Backdrops of modal surfaces: sheets from an edge — bottom sheets, the full-screen palette on
  phones, the drawer — dim the page (`Theme.scrim`: ink at 40 % in light, 62 % in dark); centred
  panels that ask for a command or a decision — the floating command palette and quick switcher,
  a centred `BDialog` — make it recede (`Theme.scrimModal`, `ThemePalette::modalScrim`: in light a
  wash of the page colour tinted 22 % toward ink at 62 %, so page text falls from about 7:1 to
  about 2.5:1 and the panel stands out by its lighter fill, ≥ 1.25:1, its shadow and its edge; in
  dark the ink at 72 %, page text about 2:1). `tst_ui_themepalette` checks both on every surface.
* Outliner look like Logseq: round bullets, thin vertical guide lines per nesting level
  (`Theme.guide`, the `border` colour: visible but quiet, about Obsidian's 1.3:1; also from a
  bullet down to its children), fold
  arrows, clicking a bullet zooms into the block (breadcrumb to zoom out, and Android's Back,
  one level each before it navigates; Enter in a zoomed heading or paragraph ends the zoom) — in
  outline mode; in document mode a note reads like an
  Obsidian page (see "Note modes" below), its nested lists with the same guide lines.
* Touch (Android, `--touch`): targets ≥ 44 px (the dense rows of navigation lists — the
  sidebar and drawer's navigation, Favorites, Recent, the page tree — are exactly 44 px, the
  iOS list minimum, other rows 48), bottom navigation, drawer, an editing toolbar
  docked on the on-screen keyboard (the bottom navigation makes way while the keyboard is up;
  when it goes, the navigation comes back and a toolbar that stays docks right on it), dialogs as
  bottom sheets on the keyboard — those one types in as cards hanging from the window's top,
  in one place ("On-screen keyboard" below).

## Window layout (`qml/Main.qml`, `qml/shell/AppShell.qml`)

* **A newer version** (`ShellUpdateBanner`, `AppController.updates`, `shell/appupdates.h`): on
  Android and Windows, where nothing updates the app by itself, a card between the top bar and the
  view, at every width and in every view, hidden while the on-screen keyboard is up: "Brulion X.Y.Z
  is available", **View on GitHub** (the release's page in the browser; the card stays until
  the app is updated) and a cross (no more message about this version; a later release shows it
  again). Settings → Application → "Check for new versions" (on by default, this device's setting)
  turns it off; GitHub is asked once a day and only while it is on. Builds with the CMake option
  `BRULION_UPDATE_CHECK=OFF` (store builds: F-Droid, IzzyOnDroid; docs/ANDROID.md) have neither
  the card nor the switch (`updates.supported` is false, also with `--update-available`).
* **Focus mode** (F8; "Statistics and focus mode" below): on a page or the journal no sidebar,
  top bar, side panel, status bar or bottom navigation.
* **Status bar** (`ShellStatusBar`, wide windows without a touch UI, not in focus mode; also at the
  bottom of a page's own window): a 24-px bar (22 dense) along the window's bottom edge in
  `bgSunken` with a hairline above, everything else ends at its top (`AppController.statusBarShown`
  tells the views). Left: the sync status (`statusBarSync`) — a dot (green `success`: synced,
  amber: syncing or offline, red: failed, grey: only on this device) and its words ("synced", or
  the text of `ShellSyncFooter`: "Syncthing folder", "Only on this device", an error); a click opens
  the sync center. Right, only what there is: the focused pane's view's `statusPosition` (PageView:
  "Block 3 of 12" while a block is edited) and `statusText` (the word count, "Statistics and focus
  mode"), both in the mono font, then the command palette's key as key caps and "commands"
  (`statusBarPalette`; a click opens the palette). The views report these through `ViewTraits`
  (`ShellRouteView.viewStatusText`, `viewStatusPosition`).
* **Wide (≥ 900 px):** the **rail** of views at the window's left edge, then a `SplitView` — the
  **sidebar** (the page panel) | **top bar** + current view | optional right **side panel**. One
  1-px hairline between them: the split handle's (`BSplitHandle`, which also highlights on hover
  and while dragging); the sidebar and the side panel draw no edge of their own there (the drawer
  keeps its edge), the rail its own line.
  * Rail (`ShellRail`, also the drawer's left column on phones; on the graphite background the
    darkest layer, without its edge line): at the top the notebook — its round mark (`BAvatar`, a
    rounded square under the pointer); a click opens the **Switch notebook** menu (the recent notebooks
    — the current one checked, one click switches — then "Brulion Documentation" and "Open another
    notebook…" (the start screen); a recent notebook whose folder is gone is not recreated: a
    warning, and it stays on the list, marked on the start screen until removed — one in a
    temporary folder leaves it); then the views as icon buttons in groups, a line between them
    (`ShellRail.startsGroup`, above the first shown entry of a group): Journal, Calendar, Tasks |
    Habits, Gym, Flashcards | All pages, Tags, Graph | Search | the extensions' entries. The
    selected one has the accent's tint and icon colour and a pill at the rail's edge (`railPill`,
    as Discord's servers; a short pill under the pointer). The entries hidden in Settings →
    Appearance → Layout are left out (no line for an empty group); in the drawer also Import notes;
    they scroll when the window is too low. **Names:** on the desktop, with Settings → Appearance →
    Layout → "Names beside the views' icons" (`AppSettings.railExpands`, `ui/rail/expands`, on by
    default; not on touch screens), the rail widens to 216 px over the page panel (a shadow, the
    rail's `Loader` above the `SplitView` and as wide as `ShellRail.faceWidth` — a pointer reaches an
    item only within its parents' bounds —, the split placed beside the narrow width, so nothing moves)
    250 ms after the pointer moved onto it (a window opening under a still pointer stays narrow),
    and narrows 150 ms after the pointer leaves:
    the notebook's name beside its mark, each view's name beside its icon, its shortcut as key
    caps under the pointer, the flashcards' count at the row's end, the sync status's text. The
    icons stay where they are. The keyboard's focus in it (Tab) widens it too, as does its
    notebook menu while open. Off, the names and shortcuts are tooltips to the right. In the
    drawer, where a phone has no tooltips, each view has its short name under the icon —
    Journal, Calendar, Tasks, …, Pages, Import, Settings —, the rail 76 px wide, as Material 3's
    navigation rail. At the bottom the sync status (`ShellSyncFooter` with `compact`: the icon,
    its text in the tooltip, beside it with `textShown`; a click syncs now when there is a sync
    engine, otherwise or while a sync runs it opens the sync center, as do a right click and a
    long press) and Settings. The sidebar button (`panel-left`, `AppShell`'s `sidebarToggle`) hides
    only the page panel: the rail stays (not in focus mode). The button never moves: it sits just right
    of the rail, level with the top bar's buttons, over the page panel's header while the panel shows
    and over the top bar's start while it is hidden — both leave it room (`leadingRoom`), so a second
    click lands on it again. The panel slides (`sidebarReveal`, `Theme.duration`): its room in the
    split narrows to nothing while the panel keeps its width, goes under the rail and fades, and the
    top bar's row moves left into that room; the width the user's split handle left is kept.
  * Sidebar (`ShellSidebar`, the page panel): the notebook's name and a new-page button, then
    its sections in the order chosen in Settings → Appearance → Layout → "Order of the sections"
    (`AppSettings.sidebarSections`, `ui/sidebar/sections`; Pages, Favorites, Recent by default;
    the ones before the tree are the list's header, the ones after it its footer): Favorites (shown
    once a page is starred — the page header's ☆, the More menu, a tree row's "Add to
    favorites", the palette); Recent (up to 5 pages opened lately, without the one shown — the
    tree highlights that one —; hidden while there are none); Pages (the folder tree — see
    "Folders" below). Favorites and Recent can be hidden there too.
  * Top bar (`ShellTopBar`): sidebar toggle, back / forward, the title (a breadcrumb for pages
    in folders), the **search field** "Search or type a command…" (the page switcher's key in
    it, Ctrl+O by default: it opens the switcher, `BQuickSwitcher` — pages by name, "Create
    page “…”", and last "Search the notes for “…”" (the search view with the query); a query starting
    with ">" turns to the command palette with the rest typed), the AI assistant's ✨ (while it is
    on: "Extensions" → "AI" below), Reading mode (the eye, on pages and
    the journal: "Reading mode" below), Quick capture, side panel toggle
    and **More** (⋯; ⋮ on phones). More: a row of the page's actions as icons with short
    names (Favorite — the star tinted when the page is one —, Rename, Move, History; a daily
    note has no Rename nor Move); **Mode**, a segmented switch Document | Outline | Source
    (the note's mode, `ShellRouteView.viewNoteMode`, and its source mode; a picture has "Show
    as text" / "Show picture" instead, a PDF or another file neither); **Open in** — New
    tab, Window, Side panel, Split (phones: "Open in a new tab"); then Add property,
    Ignore tasks on this page / Show this page's tasks in Tasks (a page's
    `zadania: ignoruj` — `tasks: ignore` with an English interface — set or removed,
    `PageView.toggleTasksIgnored`, `Workspace.tasksIgnored`; docs/SYNTAX.md §5 "Ignoring a page's
    tasks"), Note options… (a page: "Note options" below), Find in note, Show in tree, QR code… (a note: "QR code" below), Open in another app
    (a PDF, an attachment); Move to trash between two separators; on phones Command palette; the **Notebook** submenu (New page, Sync
    center, Import notes, Import and export, Replace in notebook, Clean up, Trash, About
    Brulion) and Settings.
    Under it, with two tabs or more, the **tab bar** (`ShellTabBar`), and the view: one pane or two
    side by side (the split view) — "Tabs, split view and windows".
  * Side panel (`ShellSidePanel`): a page opened "in the side panel", or the current view's
    context — the accordion of "Side panel" below (`ContentSidePanel`). While the panel lists a
    page's **Backlinks** (the section unfolded: `ContentSidePanel.currentTab` "links", copied into
    `PageView.sidePanelTab`), the page does not draw its own references section under the blocks
    (`PageView.refsInSidePanel`; the same linked references are on screen in the panel, as the
    header's chips hide while the open properties panel shows them); closing the panel, pinning a
    page there or folding the section brings it back at once. Narrow windows reach the same panel
    in a drawer from the right edge and always show the section.
* **Narrow (< 900 px, phones):** the rail and the sidebar become a `Drawer` (the top bar's ☰
  or a swipe to the right from the left edge; a swipe to the left on
  the drawer or the scrim closes it). Its content is made 0.6 s after the notebook is ready, or
  when the drawer first opens (a swipe included), not before the first frame (`AppShell._drawerMade`). The drawer is edge to edge: its `bgSunken` runs under the status and
  navigation bars, and the sidebar's own insets (`topInset` = the SafeArea's top,
  `bottomInset` = `AppShell.navigationBar`) keep the notebook button, the notebook row and the sync status clear of
  them — counted once: the Basic style pads a Drawer by the SafeArea margins itself, so the
  shell sets its top and bottom padding to 0 (on Android the doubled bars cost about 190 px of
  the tree). On touch UIs the swipe starts in a 24 px strip (`drawer.dragMargin`) that
  begins beyond what the system keeps at that edge: Android's gesture navigation takes a swipe
  from the very edge for "back" (`GestureInsets.left`, the system gesture insets reported by
  `ImeInsets.kt`, ~30 dp; 0 with button navigation), a display cutout or a system bar in
  landscape its `SafeArea` margin — so with gesture navigation the finger starts a little in
  from the edge. Taps and vertical scrolls in the strip reach the content; only a mostly
  horizontal drag past the drawer's 20 px threshold opens it, not one an item already keeps (a
  scrolling Flickable); Android's text selection handles are windows of their own above the app.
  While typing or selecting blocks (the keyboard up or an editing toolbar docked, also on the
  bottom navigation after the keyboard went) there is no strip: the drawer took the drag of the
  selection's start grip, which lies at the left edge (the top bar's ☰ opens it; a swipe on it
  still closes it).
  Nor on a view that swipes sideways from the left edge itself (the view's `edgeSwipe`,
  `ShellRouteView.viewEdgeSwipe`): the calendar's week and month grids reach that edge and a
  swipe to the right there shows the previous period; the agenda keeps the strip. While the
  drawer is open the shell marks the view `covered` and the grids' swipe is off, so a swipe on
  the drawer closes it instead of turning the period underneath. In a mobile browser
  (WebAssembly) nothing reports the system's zones, so the strip lies at the very edge, where
  Android's gesture navigation or the browser's own edge swipe (iOS Safari) may take the swipe
  for "back" first — the top bar's ☰ opens the drawer there. The strip follows the
  UI mode (`Theme.touchUi`), not the pointer: a mouse or touchpad in a touch UI (a narrow window
  on a laptop with a touch screen, Android with a mouse) gets the 24 px strip too, outside a
  touch UI it is Qt's default (the start-drag distance); the top bar keeps ☰, back, title,
  **+** (quick capture; off in Settings → Appearance → Layout, capture stays in the palette and
  under Ctrl+Shift+N), Search only while the bottom navigation has no Search tab, and More (⋮, a
  sheet: its "Reading mode" and "Show side panel" items are the narrow window's — the bar stays
  to the notebook, the title, + and ⋮).
  The drawer's button (`topBarMenu`, "Menu") is the notebook's round mark (`BAvatar`, its first
  letter), as an account's picture in a chat app; it always comes first, in the same place whatever
  the view, the history or the keyboard;
  back follows it while there is history, except on Android
  (`ShellTopBar.systemBack`), whose back key / gesture walks the same history — the browser on
  a phone and a narrow desktop window keep it;
  a **bottom navigation** of views only: Journal, All pages ("Pages"), Search, Tasks,
  Calendar by default — five views chosen in Settings → Appearance → Layout. Every item
  switches the view and shows which one is current; actions stay on the top bar, so the two
  bars never repeat a button (up to 0.13 the bottom bar also had **+ Note** and **Menu** →
  drawer, which were the top bar's + and ☰ again and behaved unlike the tabs around them).
  It is a **floating capsule** (as Instagram's and Material 3's): 56 px high, held 14 px off the
  screen's sides and 6 px above its bottom (the bar 64 px with its room, as before; its items
  46 px high) (`bottomNavCapsule`: `bgElevated` — where the
  accent's text keeps 4.5:1 on its tint —, a hairline edge and an elevation-2 shadow), the page's surface
  (`Theme.bg`) around it — the bar's room is its own, nothing of a view lies under it. Every item
  is one `NavTab`, at least 48 px wide, its view's name its accessible name and a tooltip on a
  long press (after the views up to two extensions' buttons, `bottomNav` in "Extension items in
  the UI", only while every item keeps its 48 px): the others are icons only, sharing the room evenly; the **current view's item grows
  into an accent-tinted pill** with its name beside the icon (`accentBg`, `accentText`; the width
  animated with a small overshoot), at most what the other items leave at their 48 px, so at the
  larger text sizes the name stays whole and only an extreme one elides. A pressed item shrinks a
  little (`Theme.pressScale`). The
  navigation makes way while the on-screen
  keyboard is up — when the keyboard goes while the editing goes on, it comes back (the editing
  toolbar goes with the keyboard); a tap on one of its tabs then opens that view, the editing
  ends as when leaving the page, and the top bar's + ends the editing (the view's
  `endEditing()`, as the toolbar's "Hide keyboard") before quick capture opens —, and for the drawing editor's
  tool bar, which takes its place ("On-screen keyboard"). The Android back key /
  gesture walks back
  through the history; the app closes only from the home view. Before that it peels what the
  view shows: the system first hides the on-screen keyboard; the focused part takes its own Back
  (the touch selection mode, the find bar, a drawer or menu); focus mode ends; then the view takes
  a step of its own (the view contract's `canStepBack` / `stepBack()`, ShellRouteView): in a page
  or the journal a block zoomed into zooms out one level per Back, at home too. AppShell takes
  both the press and the release of such a Back (the step is done on the release), so the
  history is not walked as well. The drawer also lists
  **Import notes** as the navigation's last entry (the import tab of `ImportExportView`), in
  the same rhythm as the entries above it.
* **More menu** (top bar ⋮): on a page or daily note — Add to favorites / Remove from
  favorites, Rename… (F2) and Move to… (on a page route — a daily note opened as a page
  too —, not in the journal views by date: "Folders" below), Show in tree (the
  sidebar, the drawer on phones), Page history, Markdown source (Ctrl+E), Toggle
  note mode, Open in side panel (wide only), Move to trash — on a picture attachment
  Markdown source is the picture bar's Show as text / Show picture and the note mode is not
  offered (nor in the palette: a picture is no note); always — Open in new tab, Open in
  new window (desktop, wide), Split view / Close split view (wide), New page (Ctrl+N), Command
  palette (Ctrl+K), **Sync center, Import notes (the import tab), Import and export,
  Trash (n), Clean up, Settings (Ctrl+,), About Brulion**. These destinations have no button of their own: on
  phones this menu (and the command palette) is the way to them. A new notebook (at most one
  note) also offers **Import notes** under the empty journal's hint, and the start screen has an
  "Import notes" row that opens the chosen notebook on its import tab
  (`AppController.openVaultAt`); outside the browser its start card says that an Obsidian vault
  (also one synced with Syncthing) needs no import: its folder opens as the notebook
  (docs/IMPORT.md "Obsidian folder without importing").
* **The documentation notebook** "Brulion Documentation" (every platform; in the browser the
  app data folder lives in IndexedDB, docs/WEB.md "Data in the browser"): Brulion notes in the UI
  language written from `src/app/shell/guide/<language>/*.md` (`en`, `pl`) into
  `<AppData>/Brulion Documentation` — with the Polish UI `<AppData>/Dokumentacja Brulionu`
  (`notebook::docsNotebookFolder()`) — and brought up to date on every open (pages the user
  changed or deleted stay so). Each folder keeps its own language; both count as the
  documentation notebook (`notebook::docsNotebookLanguage()`, also for brulion-cli). It opens
  on its table of contents from the switcher, the palette ("Brulion Documentation" / "Back to my
  notebook") or Ctrl+Shift+B, and is for reading: it is never a recent or start notebook (a restart opens the
  user's last notebook; the start drops it from the recent notebooks where 0.3.0–0.3.5 listed
  it), arms no reminders (the user's notebook keeps its armed alarms), quick
  capture and reminder taps from it go back to the user's notebook, and its Nextcloud switch
  is its own (off; it never takes over the user notebook's server folder).
* Shell popups: command palette (`BCommandPalette` over `CommandModel`: navigation, journals,
  new page, New folder…, Move to…, Show in tree, Rename page… (F2), capture, theme,
  language, sync, panels, focus mode and typewriter scrolling, settings, Keyboard shortcuts, about; the editor's commands —
  Markdown source, note mode, "Copy link to page", "Copy block reference", "Copy link
  to block", "Copy link to heading": the current block is the one being edited (a table whose
  cell is edited too; a cell left for the palette leaves its table selected), else the selected
  one, the heading that of its section; nothing current: a toast "First select a block or put the caret
  in it"; in Markdown source the three block and heading commands are disabled — the
  page's views report it as `sourceMode`, AppShell binds `AppController.sourceModeShown`), quick switcher (`BQuickSwitcher`, fuzzy page search), quick capture (one
  line → today's journal; dates in the text become ⏰ / 📅; Android's home-screen widgets have a
  quick add of their own, outside the QML UI — date chips instead of words, docs/ANDROID.md
  "Widgets"), the share dialog
  (`ShellShareDialog`, "Save to Brulion": text or `.md`/`.txt` files another app shared —
  Android's share sheet, docs/ANDROID.md "Share → Brulion", the browser version's share
  target (docs/WEB.md "Sharing to Brulion": `--share-query`, or handed over by another tab
  through `WasmShare` to `AppController.receiveShareQuery`), a desktop's "Open with →
  Brulion" (`--open <file>…`, a second launch's `share` message: `pendingShare.source` "open" —
  the subtitle says "%n file(s) to save in the notebook" and the hint `shareCopyHint` that a copy
  is saved and the file stays unchanged; a file that already is a page of the open notebook — also
  in a folder linked into it or in a folder starting with "." — opens as that
  page instead, the dialog lists only the others; opened files arriving while the dialog waits are
  added to it, a file opened again is read again, any other share replaces it), or `--share` — go
  to today's journal or to new pages; `AppController.pendingShare` / `saveShare` / `dismissShare`; a
  segmented "Today's journal" / "New page" (text: the journal by default, one file: a new
  page titled after the file, several: one journal block each), the page title field for a single
  page, the text editable before saving — never through quick capture's date parsing —, files
  listed by name with their first lines or the reason they cannot be saved, all as plain text; a
  refused save (a page whose name differs only in letter case exists) keeps the dialog open with
  the reason; "Cancel" drops the share and its copies; an extension's share action
  (`share`, "Extension items in the UI") takes the share instead — nothing saved, the dialog closes; `requestUiAction("share")` opens it once
  a user notebook is ready), new page / folder, rename, move
  (`ShellMoveDialog`), delete confirmation, about; toasts. Views reach the shell's dialogs for a
  page or folder through `AppController.requestItemAction(action, path)` (`move`, `rename`,
  `delete`, `newPage`, `newFolder`, `reveal`). No popup is made with the shell (docs/PERFORMANCE.md
  "L10"): the four used most — the switcher, the palette, the top bar's More menu and quick
  capture — are built ahead in the background 3 s after the notebook is ready, one every 0.4 s,
  each by an asynchronous `Loader` (in pieces over several frames; opened before it is ready, the
  rest is made at once: `AppShell.prebuildNext()`) — while the app is in use: away, the closed
  ones go, and they are built ahead again when it is back ("Navigation": Away); the others are made the first time they open
  (`newPageNow()`, `renameNow()`, … — the extension question with the first question, through a
  `Connections` of the shell) and kept. Before that a popup's property (`commandPalette`,
  `newPage`, …) is null and its objectName is not found. The other menus of the top bar, the rail,
  the tab bar, the tree and the breadcrumb are made when they first open (`aiMenuNow()`,
  `notebookMenuNow()`, `openTabMenu()`, `contextMenuNow()`).
* **The tray calendar** (`ShellTrayCalendar`, desktop; docs/WINDOWS.md "Tray calendar"):
  a window of its own (Qt.Tool, frameless, always on top, no transient parent — owned by a
  hidden main window it would hide with it on Windows), created on first use by Main.qml's
  `Loader`, about 336 × 500 px with a 1 px frame: today's long date and a close button,
  `ContentMiniCalendar` (the month with its dots, the picked day's tasks and reminders, "Open
  journal"; reset to today's month on every open; without a notebook only the month and
  "Open a notebook to see its tasks and reminders."), "Open Brulion" and "Quick
  capture…". An entry or "Open journal" navigates and calls `AppController.showWindow()`,
  which hides the calendar (Main.qml hides it on every `showWindowRequested`, on "toggleWindow"
  and before the main window closes, so a shown calendar never keeps a really closed app
  running). It opens from the tray (`DesktopTray`): the menu's "Calendar" (`uiActionRequested
  ("trayCalendar")`) and, with Settings → Application → "Click on the tray icon" =
  Calendar (`AppSettings.trayClick`, `app/trayClick`: "calendar" by default on every desktop), a
  single left click ("toggleTrayCalendar": an open calendar closes); a double click
  always shows the window. Placement: `AppController.trayPopupPosition(w, h)` →
  `traypopup.h` — against the work area's edge nearest to the tray icon
  (`QSystemTrayIcon::geometry()`, Windows) or to the cursor (X11, where the icon's rectangle is
  empty), centred on it along that edge, 8 px away, kept inside the work area of the anchor's
  screen. Wayland ignores a window's position and gives no click position (Qt's and KDE's
  StatusNotifierItem drop it): there `src/platform/traywindow.h` makes the window — once, before
  its first show — a layer-shell surface (KDE's LayerShellQt, in the Nix package) anchored to the
  bottom-right corner of the active screen's work area, 8 px from its edges (off the panels' reserved
  space), layer "top", keyboard on demand; a compositor without layer shell (GNOME) or a build
  without LayerShellQt (the AppImage) leaves it an ordinary window the compositor places. It hides itself — always
  `hide()`, never a close: `onClosing` refuses Alt+F4's close — on Escape, the close button, and
  when it loses the activation after having had it (a click elsewhere; a refused activation keeps
  it open without the keyboard — no Escape — until the close button, an entry or a click on the
  icon). That hide records the time and the cursor (`AppController.trayPopupHidItself`); the next
  click on the icon (`trayPopupShouldReopen`, asked once per tray action) keeps it closed when it
  is the click that took the activation: within 300 ms, or within 3 s when the hide happened with
  the cursor on the icon or within 8 px of the click's (Windows hides it on the press, Trigger
  comes on the release — a held button).

## Side panel

One component, `ContentSidePanel` (`qml/content/`), the view's `sidePanel`, in two hosts: docked
beside the text in wide windows (≥ 900 px, `ShellSidePanel` in the window's `SplitView`, the top
bar's panel button and Ctrl+\ show and hide it) and in a drawer from the right edge of narrow ones
(`ShellContextDrawer`). It is an **accordion**: a scrolling column of folding sections
(`ContentPanelSection`) — a header row with a chevron, the title in small letter-spaced capitals
(11 px, `fgMuted`) and a count in the mono font —, each section's content made only while it is
unfolded (the unlinked mentions read every note, the local graph runs a layout). What is folded is
no setting: every panel starts with the defaults below, a drawer opened again is a new panel.
`route.open` ("links", "outline", "calendar"; the page menu's side panel items) and the
`--popup context:<section>` option unfold one and scroll to it (`ContentSidePanel.reveal()`).

* **A page** — **Outline** (its headings, live while typing, indented by level; a click scrolls the
  editor there, a right-click / long press copies `[[Page#Heading]]` or its embed;
  `ContentOutline { embedded: true }`), **Backlinks** (the linked references, compact, grouped by
  page; `ContentBacklinks { sectioned: true; showUnlinked: false }`), **Unlinked mentions**
  (folded at first; "Link" per mention and "Link all"), **Local graph** (`ContentLocalGraph`: the
  page and the pages it links to or that link to it — `GraphModel` with `focusPath`, depth 1, no
  tags, at most 200 nodes, read from the page's neighbourhood only, not the whole graph, and built
  again only when one of its notes or a note linking to it changes; out of sight (`shown: false`) a
  change only marks it stale — in a 160–240 px square fitted to them; a click on a node opens it,
  "Open graph" opens the graph view on the page). A picture or a PDF has only its Backlinks.
* **A journal day** (also the journal stream: the day being written, else today) — **Day plan**
  (`ContentDayPlan`, `Workspace.dayPlan()` = `reminders/dayplan.h`: the blocks with a due date, a
  scheduled date or reminders that day, one row per block — a task due today with a reminder at
  17:30 is one row at 17:30 —, timed rows first by time; the time in the mono font or a dash, a
  3-px bar — amber for reminders and alarms whatever the accent, the link blue for due dates, red
  for a due date already past, violet for scheduled ones, faint for a done task, struck through —,
  the text, ⏰! as an icon; a click opens the block), **Tasks** (`ContentDayTasks`: the open tasks
  the Tasks view files under Overdue, Today and Tomorrow seen from that day — `TaskListModel` with
  `today` = the day —, at most 12; a ring in the task's colour, the text, its date in the mono font,
  red when overdue; "All tasks"), **A year ago** (`ContentYearAgo`: the same day a year before —
  29 February: the 28th —, its date as a link, its first six blocks rendered like a link preview,
  or "No journal on that day."), **Backlinks**, **Calendar** (folded: the month with its journal
  days, `ContentMiniCalendar`).
* **The extensions' panels** (both, after the app's own sections): every loaded view with
  `meta.panel` (docs/EXTENSIONS.md "View blocks and panels") — `"page"`, `"journal"` or `true` —
  is a section titled with its label (else its extension's title), the view drawn by
  `ShellExtensionView { embedded: true }` over an `ExtensionView` whose `notePath` is the note on
  screen (its `page`, `path` and `date` among the params) and `live` while the section is shown;
  `Workspace.extensions.panels(kind)` lists them (followed through `revision`: an approval or a
  reload adds or takes sections). The habits' `dzien` (the day's checks) is one beside days. On a
  phone they are in the drawer like every section. A view with `panel = "sidebar"` is a section of
  the pages panel instead (`ShellSidebar`'s footer, after Favorites and Recent: a collapsible
  `BSectionHeader` — its fold in `ui/sidebar/extension/<extension>/<view>` — over the embedded
  view, drawn for `currentPath`), on a phone in the left drawer.

**The drawer of narrow windows** (`ShellContextDrawer`, phones and windows under 900 px): about
85 % of the width (at most 420 px), sliding in from the right over the page, which the scrim
dims (`Theme.scrim`); a header with the page's title and a close button over the accordion. It
opens from

* the **handle**, always on the right edge while the view has a context panel (not in focus mode,
  not while the left drawer is open): a quiet grabber (a 5 × 44 px capsule in `fgFaint` at 40 %,
  80 % while pressed, 4 px in from the edge; a 44 × 64 px target) at 38 % of the height; a tap or a
  swipe to the left on it
  opens the drawer. The handle's rectangle is kept from the system's gestures
  (`GestureInsets.setExclusion()`; Android 10+: `View.setSystemGestureExclusionRects` through
  `AndroidBridge::setGestureExclusion`, so a swipe from the edge there is the handle's and gesture
  navigation's "back" keeps the rest of the edge);
* the top bar's ⋮ → "Show side panel" (`menuSidePanelNarrow`, narrow windows with a context),
  Ctrl+\ and the palette's "Toggle side panel";
* a swipe to the left that starts in the 24-px strip beyond the system's gesture zone at the right
  edge (as the left drawer's, `GestureInsets.right`).

A tap on the dimmed page, a swipe to the right on the drawer, Escape or the Back key close it; the
view gets the focus back. Wide windows never show the handle or the drawer.

## Folders (the sidebar's tree, "Move to…")

Folders are namespaces of pages (docs/SYNTAX.md §1); moving never breaks a link (§2 "Rewriting on
rename and move"). In a link to a page in a folder (`[[Projekty/Dom]]`) each path part is a link
of its own: "Projekty" opens the page Projekty, or — when there is none and the folder holds
notes — the folder's pages in All pages (`{view: "allPages", path}`; SYNTAX §2 "Path parts", "Links
in rendered text" below).

* **The tree** (`FolderTreeModel` in `ShellSidebar`): lazy (rows only under open folders);
  folders first, then pages, in Polish order with numbers by value (`text::compareNames`, the
  same in All pages and on every platform); the journals folder first, with a calendar icon,
  its days by readable dates ("Wed, Oct 14", in the language shown — a switch relabels them),
  newest first, closed by default; attachments hidden
  unless "Show attachments". Indent guides as in the outline: a row at depth *d* draws a 1-px
  line under each ancestor's chevron (x = 14 / 10 px + `indent(level)` + half an icon), over its
  full height, so the lines run on from row to row, below the row's background (the selected
  row's pill covers them), in `Theme.guideOnSunken` (`borderStrong` in the light, `border` in the
  dark: the tree lies on `bgSunken`); both densities and the 8-px steps below the fourth level. Vault changes reach it as inserted and removed rows (never a reset
  while rows stay: the list keeps its place, Syncthing's deliveries included) — only changes of
  the list of files (`Workspace::fileListChanged`: a file or folder created, deleted or renamed),
  never an edit of a page's text (every autosave); open folders are
  remembered per notebook (device-local), follow renamed and moved folders and are forgotten
  with deleted ones. A tap opens or closes a folder and opens a page; the open page or daily
  note is revealed (its folders opened, scrolled into view) and highlighted on every route
  change — the "+" menu's "Show the open page" (`ui/sidebar/revealCurrent`, on by default)
  turns that off; a collapsed "Pages" stays collapsed (revealed when opened again), only "Show
  in tree" opens it. "Pages" counts pages without the daily notes. The revealed row (also a
  new folder's, "Show in tree"'s) stays wholly in the list's visible part, above the system's
  navigation bar: `ShellSidebar.revealedPath` is placed again (`placeList()`: the top unless the list
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
* **Menus**: right-click, press-and-hold, the Menu key (then ↓ / ↑ and Enter, as in every menu:
  "Design system in code"), and on touch screens a "⋯" button on
  every row (44 px; press-and-hold stays), drawn faint (`fgFaint`, like the rows' icons; `fg`
  while pressed) so the column of buttons stays quieter than the page titles. A folder: New page here, New folder…, Show
  folder pages (All pages in it), Search in folder (search in it), Move to…, Export
  folder… (the export tab, folder scope), Rename…, Move to trash (the confirmation
  counts its pages and warns for the notebook's special folders). A page: Open in side
  panel (wide), favorites, Move to…, Copy link (`PageTools.wikiLink`: the page part of every
  copied link, `notebook::pageLinkTarget` — the shortest target, a daily note by its file name), Rename…, Page history, Move to trash. Before Move to trash
  both have the extensions' items (`folderMenu`, "Extension items in the UI"). A daily note is a
  file here like any other — renamed, moved and dragged (a misfiled day can be put right) — also
  in All pages, F2 and the More menu of a daily note opened as a page; only the journal view
  (by date) and the page's own menu there offer neither. The "Pages" header's "+": New page,
  New folder (at the root), Collapse all, Show attachments, Show the open page.
* **Drag and drop** (mouse and touchpad only — a finger keeps the list's flick, press-and-hold
  and the drawer's edge swipe): a row dragged onto a folder (a closed one opens after 600 ms; the
  list scrolls near its edges, the target following the rows that pass under the pointer) or a
  page (its folder), or onto the "Pages" header while it shows (the root), is moved there by
  `Workspace.movePath`; the ghost names the target and, a moment later (150 ms on one target),
  what the move rewrites ("Updated links: 3 (in 2 notes)", the settings that follow) or
  why it cannot move there — `Workspace.requestMoveImpact()`, planned a few milliseconds per frame
  (`RelocationPlanner`) and dropped when the target changes, so a big folder never stalls the
  drag; impacts are kept per target until the notebook changes. The target is outlined, Escape
  cancels. The toast has Undo (moving it back).
* **"Move to…"** (`ShellMoveDialog`; a bottom sheet on phones): a search over the folders
  (fuzzy: names, then paths), "Notebook root" first, the current folder shown but
  disabled, a folder's own subtree left out, and "New folder “…”" for the name typed (created,
  then moved into). A chosen folder shows what the move rewrites (`Workspace.moveImpact`:
  "Updated links: 3 (in 2 notes)", the special-folder settings that follow, or why it
  cannot move) before "Move here". From the tree, the More menu, the page's menu, All
  pages' rows and the palette. Its pick mode is the new-page dialog's folder picker.
* **Rename** (`ShellRenameDialog`): a "/" moves — a path from the notebook root ("Archiwum/Dom"),
  with a live "New place: …", and its toast names the new name too when the move renames
  ("Moved to “Archiwum” as “Dom 2”"); for any name typed it counts exactly what is rewritten
  (`Workspace.renameImpact`: also links a new name would otherwise take over), hidden folder
  names are refused. What is wrong with the name typed (taken by a page or folder there, hidden)
  shows at the field, a moment after typing stops, Rename disabled; the subtitle only says what
  changes. The page header's title edit renames in place: a "/" there is part of the
  title ("TCP/IP" -> "TCP IP.md"), and its confirmation counts the same way.
* **New page** (Ctrl+N, the top bar, the sidebar's pencil): in the folder of the page shown, as a
  removable chip "In folder: Projekty ×" (a page at the root: "In the notebook root ×")
  with "Change…"; without it the notebook's folder for new pages. Pages created from `[[links]]`
  keep `newPagesFolder`. A new folder is shown in the tree. What would keep Create from making
  one is said at the field while typing, Create disabled, by the core's own checks and in its
  words (`Workspace.folderProblem`: `notebook::plannedFolder`, which `createFolder` runs too): a
  folder of the name there already — in any case, the name sanitised and NFC ("projekty",
  "Projekty?", a decomposed "ó"): "A folder named “Projekty” already exists here.", as
  `createFolder` would only show it —, a name that would be hidden, a file in the way.
* **All pages** with a folder: its subfolders as rows above its pages (at the root the top-level
  folders; a tap opens one, back returns), "With subfolders" (pages of subfolders too, on by
  default, remembered per device: `allPages/subfolders`), and the top bar's breadcrumb
  ("All pages › Projekty › …"). The journals folder (and its year folders) lists its daily
  notes, in date order, without the "Journals" chip — a journal's breadcrumb leads there.
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
  reports it), `lastOverlap` (the last keyboard that settled up, kept once it went: the room a
  dialog one types in leaves for it), `source`. Sources by priority: a simulated keyboard
  (`--keyboard <px>`, the self-tests), the platform's own report — Android's IME insets
  (`android/src/…/ImeInsets.kt`, every frame of the slide on Android 11+, where the window is drawn
  behind the system bars and keeps its size — Qt's expanded client area, set on Android 11–14 by
  `keyboardsources.cpp`, always so on 15+; Android 9–10 resize the window once, no frames;
  docs/ANDROID.md "On-screen keyboard"), the browser's viewports (`setViewportSample`, computed
  by `ViewportKeyboard`: the part of Qt's container the visual viewport leaves out; the page asks
  for `interactive-widget=resizes-content`, so the browser shrinks the page and Qt's window itself
  — overlap 0 — and `shown` comes from that shrink on a touch screen while Qt's keyboard field has
  the focus (held through a focus pass until the readings settle; one tallest page height per
  width); `animating` until the readings settle; docs/WEB.md "On-screen keyboard") — and, as
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
  keyboard's own key) the navigation comes back and the editing toolbar goes with the keyboard
  (`BlockEditor.keyboardDismissed`); a tap on the text brings both back, a tap on one of the
  navigation's tabs navigates as ever (the page view goes, the editing with it). A keyboard that slides over the window (`KeyboardInsets.slidesOver`:
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
  already made room for it, so the view and the toolbar move once. While typing — the keyboard
  is up, or an editing toolbar is docked in the view or the side panel —
  without the bottom navigation (a wide touch window) the content ends above the system's
  navigation bar. That bar (`AppShell.navigationBar`, which the bottom navigation pads itself
  with) is the window's SafeArea margin — except one taller than any navigation bar (120 px, as
  `ViewportKeyboard::MinKeyboard`): on Android Qt's SafeArea counts the keyboard in at times (336
  px with it up, and for a moment after it went), which would lift the navigation's tabs into
  the middle of the screen; the last margin below that counts then. A view with a bar of its own
  at the bottom instead (`replacesBottomNav`: the drawing editor's tool bar on phones) keeps the
  navigation away. Views tell the shell with
  `toolbarDocked` and `replacesBottomNav` (view contract, `ShellRouteView`). Once the keyboard
  has settled, the detailed event log notes where the view ends ("Typing: the content ends 88 px
  above the window's bottom (keyboard 0 px, navigation bar 24 px, bottom navigation 88 px),
  editing toolbar docked").
* **The touch editing toolbar** (`MobileEditToolbar`) docks on the bottom edge of a dock host that
  never scrolls (`BlockEditor.toolbarHost`): the editor itself on a page, the journal stream's
  frame for its days (a day's toolbar used to scroll with the day and sit at its bottom in the
  middle of the screen). So it sits on the keyboard, follows its slide frame by frame where the
  platform reports the slide (Android 11+; elsewhere — the browser too, where the window itself
  shrinks — it moves once; no animation of its own), and
  goes with the keyboard when the system hides it while editing goes on
  (`BlockEditor.keyboardDismissed`: only a keyboard that was up — with a hardware keyboard the
  toolbar stays on the bottom edge —, once it has settled, and only while the edited text keeps
  the keys: a menu or dialog that took them hid the keyboard, and the toolbar stays docked under
  it, on phones on the bottom navigation, which comes back); a tap on the text brings the keyboard
  and the toolbar back. "Hide keyboard" ends the editing and takes it away. Its tools follow
  `BlockEditor.toolbarMode`: `block` (with the extensions' buttons before Undo: `keyboardToolbar`,
  "Extension items in the UI"), `cell` (a table cell — Undo, Redo, Next cell,
  Hide keyboard; `BlockEditor.editingTable`; after Undo / Redo the same cell goes on being
  edited, and every cell tool first commits the word the keyboard is still composing),
  `source` (source mode — `[[`, `#`, `/`, the source's own Undo / Redo, Hide keyboard),
  `selection` (the touch selection mode: `SelectionToolbar`, "Selecting and copying" — no keyboard,
  the tools at the bottom, on phones right on the bottom navigation).
  Taps beside the buttons stay on the bar. The slot is one Loader per editor (`editToolbarSlot`,
  created on the first use) and each mode's row is made the first time it is used, then kept
  (hidden between edits, where it was scrolled; another mode's row starts at its beginning — the
  block's Select, at the end of its row, left the selection's Done scrolled out of sight): another
  tool set docks the same way as another mode with its own row, a new button goes into its mode's
  row.
* **Form fields** (single-line fields and text areas: settings, sync and import forms,
  onboarding, dialogs) have no toolbar; nothing pans the window to them any more (Android:
  `adjustResize`), so `BKeyboardFocusKeeper` (once in `Main.qml`) scrolls the Flickables around
  the focused `BTextField` / `BTextArea` (`keepAboveKeyboard`) until it is above the keyboard —
  when it gets the focus, when the keyboard has settled, when the window or the caret moves,
  and when the innermost Flickable around it gets another height: the shell lays its view out in
  steps when the keyboard comes (taller without the bottom navigation, then ending at the
  keyboard), and a Flickable with a short content pulls a scroll past its end back at each step
  (a field near the end of a settings section).
  Onboarding ends at the keyboard like the shell's content. A word the keyboard is still
  composing (Android's keyboards keep every word as preedit text, outside `text`, until a space)
  counts as typed: dialogs enable their accept button by `typedText` (quick capture's "Add",
  "Create" of a new page or folder, rename), and the
  word becomes text when the field loses the
  focus (`EditorInput.commitPreedit`) or the dialog is accepted (`BDialog.tryAccept`) — a single
  word typed into an empty field used to leave the button disabled.
* **What is being edited stays above the toolbar**: the caret or the table cell
  (`BlockEditor.ensureEditVisible()`, the journal's `ensureVisible()` with the toolbar's height;
  a day's editor asks the journal list, `BlockEditor.scroller`), checked again when the view's
  height changes — not while the keyboard slides, once it has settled; the source's caret
  likewise. The page drawn by Atrament ends at the toolbar's top while a toolbar of typing is
  docked on it (BlockEditor's `atramentLoader`): its edited block's caret or table cell is kept in
  view as Atrament's field keeps its caret when the window gets another size (docs/ATRAMENT.md
  "Input methods").
* **Popups**: bottom sheets (`BDialog`) sit on the keyboard and centred dialogs stay above it —
  except dialogs one types in (`BDialog.textEntry`: a field that takes text shown in it,
  `EditorInput.takesText`, or the keyboard came up while it is open): on a touch UI they hang from
  the window's top (`steady`; on phones a card instead of the bottom sheet, no drag handle) and,
  once the keyboard is expected (the first focus takes text, or it came), end above the room the
  last settled keyboard took (`KeyboardInsets.lastOverlap`) also while it is hidden or sliding
  — the field and the buttons never move while the keyboard comes, grows a suggestion strip or
  goes (a sheet that followed the keyboard's top jumped under the finger while typing);
  menus (`BMenu.bottomMargin`) stay above it, the editor's task status menu also above the
  toolbar, and below the status bar of an edge-to-edge window (`BMenu.topMargin`, the window's
  safe area: on Android a finger's tap there reaches the status bar, so a block menu opened
  above the toolbar used to leave its first items untappable; one taller than the room left
  scrolls). A menu opened right above the toolbar (its More, its task button held down) takes
  the keys, so the system hides the keyboard and the toolbar drops onto the bottom navigation
  (to the bottom on a wide window): the menu
  follows it (`BlockEditor.followToolbarWith`), with the room the keyboard left — every item
  shown, "Delete block" no longer scrolled away; a menu taller than that room takes all of it, from
  below the status bar, and scrolls — until something in it is pressed or scrolled
  (`BMenu.touched`): it never moves away from a finger aiming at it. Autocomplete is placed above
  the toolbar (`BlockEditor.visibleBottom()`); toasts above the keyboard, the bottom navigation
  and the toolbar (`AppShell.toastClearance`), except while text is typed — the keyboard up, or a toolbar
  docked while a view edits text (`AppShell.toastsAtTop`): then they show at the window's top,
  one at a time (`BToastHost.atTop`), since the caret's line is kept right above the keyboard and
  its toolbar, where they covered it ("Merged…" while typing on a phone); the command palette
  ends at it.
* **Tests**: `--keyboard <px>` simulates a keyboard, drawn as a grey `BKeyboardPlaceholder`:
  `tst_editor_qml_keyboard_touch` (the playground's `keyboard` script: a block in an outline and
  a document, a slide frame by frame, the system hiding the keyboard — the toolbar with it —, typing, autocomplete,
  source mode and a tap beside the bar's buttons, a table cell — a word being composed, undo and
  redo in the cell —, the tool rows kept between edits), the journal stream's self-test (`tst_content_journals_qml`: the
  toolbar on the keyboard, not at the day's bottom, while the list scrolls or is flicked),
  `tst_shell_qml` (the bottom navigation, the view's end, quick capture at the top staying put
  while the keyboard changes and goes, a toast; editing with the
  keyboard never up (a hardware one): the bottom navigation shown and the docked toolbar right on it, also over a
  system navigation bar — the shell's `SafeArea.additionalMargins` —, + on the top bar, a
  tap on it ending the editing and opening quick capture;
  the keyboard sliding in frame by frame leaving the navigation under it until covered, sliding
  away uncovering it and taking the toolbar with it once gone, a keyboard shorter than the navigation leaving the toolbar where it was,
  a browser's keyboard — viewport samples, the page resized — taking it at
  once; a tap on Calendar while editing ending the editing; a toast while typing at the top,
  away from the caret's line, a settings field scrolled above the keyboard, a page in the side
  panel of a wide touch window; the drawing editor's tool bar keeping the navigation away),
  `tst_keyboardinsets` (`slidesOver` per source, a simulated slide down),
  `tst_editor_qml_blocklinks*` (the block menu from the
  toolbar's More following the toolbar down when the keyboard goes, every item shown, and
  staying put once touched),
  `tst_appio_import_qml` (onboarding's folder field); screenshots
  `page-editing-keyboard`, `journals-editing-keyboard`, `capture-keyboard` (and `page-editing`:
  no keyboard, the toolbar on the bottom navigation); on Android
  `scripts/android-emulator-test.sh --keyboard` (Back: the navigation's tabs back, the toolbar gone,
  a tap on the text: the keyboard back, the toolbar on it, the tabs gone; and `--folders`: the
  toolbar's "More" and the block menu with a real finger), in the browser
  `scripts/wasm-smoke.mjs --keyboard` and `--keyboard-phone` (the keyboard hidden: the bottom
  navigation back and no toolbar, `bottomNavTop` and `toolbarBottom` in the self-test's report) and
  `scripts/android-emulator-test.sh --web-keyboard`.

## Keyboard shortcuts (desktop)

| Keys | Action |
|---|---|
| Ctrl+K | command palette |
| Ctrl+O | quick switcher (go to a page) |
| Ctrl+Shift+F | search |
| Ctrl+J | today's journal |
| Ctrl+Alt+W / Ctrl+Alt+M | this week's / this month's note (docs/SYNTAX.md §1 "Weekly and monthly notes"; palette: `journal.week`, `journal.lastWeek`, `journal.nextWeek`, `journal.month`, `journal.lastMonth`, `journal.nextMonth`) |
| Ctrl+N | new page |
| Ctrl+Shift+N | quick capture (from the documentation notebook: back to the user's notebook first) |
| Ctrl+Shift+B | the documentation notebook / back to the notebook used before |
| Alt+← / Alt+→ | back / forward (in the focused tab) |
| Ctrl+T | new tab (at home) |
| Ctrl+W | close the focused tab (a page's own window: that window) |
| Ctrl+Tab / Ctrl+Shift+Tab (Ctrl+PgDown / Ctrl+PgUp) | next / previous tab (round past the ends) |
| Ctrl+1 … Ctrl+8 / Ctrl+9 | that tab / the last tab |
| Ctrl+\\ | side panel on/off (wide windows) |
| F2 | rename the current page |
| Ctrl+P | print the current page ("Exporting and printing a page") |
| Delete on a focused row of the sidebar's tree | move that page, file or folder to the trash (after the confirmation) |
| Ctrl+, | settings |
| F8 | focus mode on / off ("Statistics and focus mode") |
| Ctrl+Shift+E | reading mode on / off ("Reading mode") |
| Ctrl+E | Markdown source mode of the page / journal |
| Ctrl+F / Ctrl+H | find in the note — in the journal stream over its days (the PDF viewer and the graph keep their own Ctrl+F) / with its replace row ("Find and replace") |
| Ctrl+; | add a page property (the properties panel's add row; "Page properties") |
| Alt+1 … Alt+9 | (in the AI chat) its list's first nine conversations; ↑ / ↓, Enter and F2 in the list ("AI chat") |
| Ctrl+Enter | (in dialogs) accept |

Every key of this table but Delete, Alt+1 … Alt+9 and Ctrl+Enter is a command's shortcut and can be changed
("Custom shortcuts" below); the block editor's keys below cannot. With Vim keys on, the editor's keys are Vim's
first ("Vim mode" below).

Keys go from the focused item up to its parents, so the keyboard focus must stay inside the screen
(`stage`, the Loader of `Main.qml`): on an item above it — the window's content item, its content
control, the root item — no key (Tab, Escape, the Android back key) reaches the shell. A closing
popup can leave it there: it hands the focus back to what had it when it opened (the content item
when quick capture opened at start-up, before the window was active) and holds it on the root item
during its exit transition, after which Qt gives it to the next popup that takes the focus (a
submenu's menu) or the content item — not always: closing the assistant's question and the ✨ menu
once left it on the root item, and Esc did not end focus mode until a click. `Main.qml` gives it
back to the screen (`stage.forceActiveFocus()`) as soon as it is above it — at once, or, while a
popup that takes the focus is shown (`DeviceInfo.focusPopupShown`, closing ones too), once the
transitions are over (`Theme.durationSlow` + 120 ms, asked again while such a popup stays).
The shell self-test (`checkFocusAndShortcuts`) puts the focus on the root item and expects it back.

A hidden view never has the keyboard: typing would go into a note that is not on screen. Qt's
overlay remembers the item that had the focus when a popup opened, to give it back when a popup
that took the focus closes, and keeps it when that popup goes while shown (a top bar button's
tooltip removed with the extension's item under the pointer): the next dialog closed, on whatever
view, hands the focus to that item — a journal day's block edited back then, its view kept hidden
since. So each kept view's `Loader` (`ShellRouteView`'s `KeptLoader`, "Navigation" below)
passes the focus on the moment anything inside it gets it while hidden — such a restore, a view's
own `forceActiveFocus()` — to the view shown (`ShellRouteView.focusShownView()`), whose editor gets
it back where it was. The shell self-test's `checkHiddenViewFocus` reproduces it (a popup gone while
the journal was edited, the rename dialog closed over a page, the hidden journal's editor given the
focus outright).

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
| Alt+Enter | follow the link at the caret (in `[[Folder/Page]]` the path part under it: `PageController.linkAtCursor`; also in a table cell's field, `linkInText`); no link there: the toast "There is no link at the cursor"; never a line break | — |
| Alt+Shift+Enter | the same in the side panel (wide windows; narrow: as Alt+Enter) | — |
| ↑ / ↓ | on the first / last line: to the previous / next block | select the previous / next block alone |
| Ctrl+Enter | cycle the task status (none → todo → doing → done → none) | same (every block gets the next status of the first one) |
| Ctrl+Shift+Enter | the task status menu at the caret (any status, or none; list items and paragraphs only) | the same menu under the block |
| Ctrl+. | fold / unfold | same |
| Ctrl+Shift+C | copy the block reference `((id))` (an id is added when missing; the browser may keep the keys for its own tools: the palette's "Copy block reference" then) | same (the focus end); nothing selected: a toast says what to do |
| Ctrl+Z, Ctrl+Shift+Z / Ctrl+Y | undo, redo (per page) | same (undoing a deletion selects the blocks again) |
| Esc | close the suggestions, else stop editing and select the block | deselect (also Android's Back in the touch selection mode) |

## Habits

`qml/views/HabitsView.qml` (route `habits` {month?: "YYYY-MM", open?: "add"}; the sidebar's
**Habits** under Tasks, a bottom-bar tab to choose, the palette's "Habits" `go.habits`, the tab icon
`flame` — the sidebar's entry and the palette's command only while habits are on): recurring points
of the day recorded in the journal (docs/SYNTAX.md §5 "Habits"). A view of its own rather than a
section of Tasks or the Calendar: habits are not tasks with dates (one line a day, not one task),
and their month grids need the width.

* **The built-in extension `nawyki`** (`src/core/extensions/builtin/nawyki.luau`, docs/EXTENSIONS.md)
  computes and writes everything; the view is its view `month` in an `ExtensionView`
  (`paramKey: "month"`), drawn by `ShellExtensionView` ("Extension views" below) — no C++ of its
  own. It is on by default (`AppSettings.habitsEnabled`, `extensions/nawyki`; Settings →
  Extensions). Off, the view is an empty state, "Habits are off", with **Switch on**.
* **Header** (QML, the list's `header`): the title, "Recurring points of the day, ticked in the
  journal", **Add habit** (primary; an icon alone on narrow windows) and the month row — ‹ the
  month's name › and, while another month is shown, **This month**; the view's error, when its
  `render` fails, under it. Keys: N adds a habit, ← / → and PgUp / PgDn move the month. `month`
  in the route shows that month; `open: "add"` asks Add habit's form once the notebook is read
  (screenshots).
* **Lines** (what `nawyki` returns): "2 of 3 done today" (and "· 1 with a problem"), then a card per
  definition in the notebook's order (by path, then as written): a row of today's check (a tap
  ticks it, again opens it; a right-click or long press: Done today, Skip today, No check today,
  Open its definition — the page at its line —, Open today's journal), the name, the schedule in
  the UI language muted and the "⋯" menu with the same items; a flame with "Streak: 5 days ·
  longest: 12 days · completed: 80%" (and "2 of 3 this week" for N times a period); the month grid
  — done days in the success tint, missed due days in the danger tint, skipped ones struck out, a
  due day with a thin ring, today with the accent ring; a tap on a past day ticks it or opens it
  again, a right-click or long press offers Done, Skipped (no streak lost), No check, the day's
  journal and the definition. A definition not understood or given twice is a card with its
  problem in the danger tone and a link to it, without statistics. No habits: an empty state
  (flame) that explains the syntax and **Add habit**.
* **Writing**: the view's action `set` {name, page, date, state} writes the day's check
  (docs/SYNTAX.md §5 "What Brulion writes"), the daily note created from the journal template only
  when something is written; its result is a toast ("Done for today: Reading", success) whose
  **Undo** runs `set` again with the state before. A tick in the journal editor counts the same:
  the view is drawn again when the index changes.
* **Add habit** (the header's button, N, the palette's "Habits: add a habit"): `nawyki`'s command
  `add`, a `b.form` of two text fields (ShellExtensionQuestion, "Questions" below) — the name ("e.g.
  Meditation") and how often ("every day, Mon Wed Fri, 3 times a week…"). A name in use, none, or a
  schedule not understood is asked again with the reason under the title and the text typed kept;
  an empty schedule is every day. It appends `- Name` + `  habit:: schedule` to the page of the
  first definition, else to a new `Habits` page (`Nawyki` in Polish notes) where new pages go; the
  toast ("Habit added to “Habits”") has **Open**. "Habits: tick a habit" (the palette) asks which of
  today's open habits is done.

## Gym

`qml/views/GymView.qml` (route `gym` {tab?: "today" | "progress" | "volume" | "weight" | "catalog" |
"exercise", exercise?, id?, weeks?, range?, open?: "start"}; the sidebar's **Gym** while it is on —
Settings → **Extensions** (`AppSettings.gymEnabled`, off by default; the view says so with a "Switch
on" button) —, a bottom-bar tab to choose, the palette's "Gym" `go.gym`, the icon `dumbbell`): the
built-in gym extension (docs/EXTENSIONS.md, docs/SYNTAX.md §5 "Gym") through its views, drawn by
`ShellExtensionView`; the route's other keys are the view's params (`ExtensionView.setRouteParams`),
and a view the extension opens is a route of the gym (`tabRoute`). The header: the title, **Start a
workout**, the tabs **Today · Progress · Volume · Weight** (`BSegmented`, objectName `gymTabs`: on a
phone they keep their labels, their icons go first), the rest timer's banner.

* **Today** (view `today`): every exercise line of today's `#trening` blocks as a card
  (`b.ui.card`) of the same three lines in every card, however long its texts — the tasks'
  checkbox (done / open), the exercise's link (opens its page, made when missing), its sets; the
  last set's weight and reps as steppers (− value +, the value typed in as well: "Extension views"
  below; a line with no weight written takes the weight for every set; a bodyweight line: reps
  only); **+ set** / **− set**.
  Each control rewrites that one line of the note (`b.replaceBlock`); a line changed elsewhere
  meanwhile is not overwritten (a toast says so and the view shows it again). Adding a set starts
  the **rest timer**: a banner with the time left, a bar and **Skip**; at the end a notification —
  armed on the platform when it starts, so on Android it comes with the app closed too
  (docs/EXTENSIONS.md "Timers") — and a toast. Under the cards the **Exercise catalog**. Without a
  workout today: an empty state, **Start a workout** and the catalog.
* **Progress** (view `progress`): every exercise of the history with its record (estimated
  one-rep max) and its number of sessions; one exercise (`exercise`): its record, a line chart of
  the estimated one-rep max and the heaviest set per session (`ChartView` fed directly), its
  sessions newest first (date, sets, volume, estimate) and a link to its page. Weights in the
  gym's unit (kg or lb, its setting).
* **Volume** (view `volume`; the palette's "Gym: weekly volume" `extview.silownia.volume` opens this
  tab): sets × reps × weight per muscle and week (docs/SYNTAX.md §5 "Gym": primary muscles whole,
  secondary half, bodyweight sets with the logged body weight). The period (4, 8, 12, 26, 52 weeks:
  a select); tiles of this week's volume, the average of the weeks before it (from the first week
  with a workout), this week's sets and the difference in per cent (success when above); a bar
  chart of every week's volume; a **heat map** (`b.ui.heatmap`) — a row per muscle, the busiest
  first, a column per week, each cell tinted against the row's own largest week, this week's volume
  at the row's end (its sets for a muscle trained only with timed sets), a cell's words ("czworogłowe
  uda, tydzień od 16.11: 2500 kg, 5 serii") on hover or a tap —; a table of sets, muscle, this
  week and the average; the exercises whose muscles nobody knows, as links to their pages (to add
  `miesnie::`). No workout in the period: an empty state.
* **Weight** (view `weight`, "Gym: body weight"): the body weight log. A field takes a weigh-in as
  typed — `82,5`, `82,5 kg 18 % rano`, `181 lb` — and writes `- 82,5 kg 18 % rano #waga` at the end
  of today's note (a toast with **Undo**; "Not understood" for anything else). Tiles of the latest
  weigh-in (with its day), the trend, the trend's change over 7 and 30 days, the latest body fat;
  the range (30 days, 90 days, a year, all: a select); a line chart of the weigh-ins with the trend
  line, one of the body fat when there are two; the last 14 weigh-ins as rows with a menu (Open the
  day, Remove).
* **Catalog** (view `catalog`, "Gym: exercise catalog"; no tab: Today's button, the palette): a
  search field (any words of a name in either language, any case, without diacritics; Enter or
  **Search**), selects of a primary muscle, an equipment and a kind, the number found and **Clear
  the filters**; a card per exercise — its name (opens it) with a "⋯" menu (To today's workout,
  Create the exercise's page, Details) and its muscles and equipment under it —, 30 at a time
  (**Show more**). Brulion's own 51 come first, then names translated to the interface's language.
* **Exercise** (view `exercise`, `id`): its name in the notes' language and the other one, its
  primary and secondary muscles, equipment, kind and level; its two pictures from free-exercise-db
  side by side — "Start" and "Finish" under them, "Pictures from free-exercise-db" above —
  (`b.ui.pictures`: from the device's cache; until they may be fetched "Picture from
  raw.githubusercontent.com" in each box and **Show the pictures** under them, as a note's picture
  from the internet waits for "Show"; docs/SYNTAX.md §2, §5 "Gym"); **To today's workout** (an open line
  under today's workout, else a new one: `- [ ] [[Wiosłowanie sztangą]] 3×10`, cardio `20 min`,
  stretching `2×30 s`), **Create the exercise's page** (the catalog's properties and instructions:
  docs/SYNTAX.md §5) or the link to it, its sessions (to Progress), the instructions as a numbered
  list: in a Polish interface the Polish translation, free-exercise-db's English in an English one.
* **Start a workout** (the header's button, the palette's "Siłownia: zacznij trening"): asks which
  plan (a page tagged `#plan-treningowy`) and appends it to today's note with progression.

## Flashcards

`qml/views/FlashcardsView.qml` (route `flashcards` {tab?: "decks" | "review" | "stats" | "notes", deck?,
group?: "tag" | "page" | "folder", ahead?, more?}; the sidebar's **Flashcards** under Gym and a
bottom-bar tab to choose — both with the number of cards to study now on the icon's corner
(`WorkspaceExtensions.flashcardsDue`, an accent `BBadge`) —, the palette's "Flashcards"
`go.flashcards` with that number under it, "Review this page's cards" `flashcards.reviewPage` on a
page; the icon `layers`): cards written in notes and scheduled with FSRS (docs/SYNTAX.md §15).

* **The built-in extension `fiszki`** (`src/core/extensions/builtin/fiszki.luau`) finds the cards,
  schedules and writes everything; the view is its views `decks`, `review` and `stats` in an
  `ExtensionView` (the route's keys as params through `setParam`) drawn by `ShellExtensionView`. On by
  default (`AppSettings.flashcardsEnabled`, `extensions/fiszki`; off: an empty state with **Switch
  on**). Settings → Extensions → Flashcards: new cards a day (5–50, 20) and the desired retention
  (80–95 %, 90) — **device** settings —, then the **notebook's** (`Workspace.setConfig({flashcards:
  {…}})`, `.brulion/config.json` "flashcards", docs/SYNTAX.md §15 "Settings of the cards"; fields
  and switches disabled while no notebook is open): the tags of decks ("flashcards, fiszki"; the
  device's value stays what a notebook without tags shows and uses), the card separators (single-line
  and both ways, multi-line and both ways — an empty field switches one off), the end of a
  multi-line card, switches for clozes from `==highlights==`, `**bold**` and `{{curly}}` text and
  for **Folders as decks**, the **tags of notes to review** ("review"; `flashcards.reviewTags`), the
  notes' review algorithm — **Base ease of notes**, **Easy bonus**, **Interval after Hard**, **Longest
  interval of notes** (number fields: a dot or a comma; a value out of its range is refused under
  the field) and **Balance the notes' review days** —, and the code as a page ("Edit the code"). A field follows the config (a
  sync's change shows) and saves when editing finishes. `AppController` gives the extension both
  with every view, action and command: `WorkspaceExtensions::setFlashcardSettings` (`withSettings`)
  gets the device's map with the notebook's keys over it, again on `Workspace::configChanged`.
* **Header** (QML): the title, "N cards to study now" (else what flashcards are), **Study**
  (primary; an icon alone on narrow windows) — all cards, or the deck of the statistics —, the tabs
  Decks · Review · Statistics (`BSegmented`), on Decks the grouping By tag · By page · By folder, on
  Review and Statistics the deck as an accent chip with an × back to every card; the view's error
  under it. Escape goes back to the decks.
* **Decks**: four tiles (`b.ui.stats`: due, new left today, learning, reviewed today), **Study all**
  (Enter) and **Statistics** (`b.ui.open` drawn as buttons), then a row per deck — sub-decks of a tag
  indented under it — with its counts (accent while something is due) and a "⋯" menu (Study, Study
  ahead, Statistics, Open the page); a tap on a deck's name starts its review. No cards: an empty
  state explaining the syntax. Above the decks, while any note has a review tag, a card **Notes to
  review** with its counts ("3 due · 2 new · 6 in all") opens the notes' view; with notes but no cards
  that card is all the view shows.
* **Notes to review** (tab `notes`, the extension's view `notes`; Escape goes back to the decks, whose
  tab stays marked): the Spaced Repetition plugin's whole-note reviews (docs/SYNTAX.md §15 "Notes to
  review"). A progress bar ("1 reviewed · left: 3 due, 2 new"), the note as a card — its page name
  muted, the note after its frontmatter as `b.ui.markdown` lines of about 3000 characters cut at blank
  lines outside code fences (a very long note stops at 100 000 characters with "The rest of the note
  is in the editor.": the view scrolls like any other, no inner scroll area) —, then the three
  grades, Hard (warning) · Good (success) · Easy (info), each with the days it would schedule under
  its label ("12 dni") and its key 1–3, under them **Skip** (S: the note is left out of this session; "Show the skipped notes" brings them back), **Undo** (U: the note's lines written back,
  also after skips), **Edit** (the note), the state ("review · today", "new note") and a menu (Open
  the note, Decks). Done: "All notes reviewed for today". A grade writes the note's frontmatter
  through the extension; a header that changed meanwhile (the three lines compared) is not
  overwritten ("The note's header changed in the meantime", the note shown again). No review log
  line, no daily limit, and the sidebar's badge keeps counting cards only (`count` also returns
  `notesDue`, `notesNew`, `notesTotal`).
* **Review**: a progress bar ("3 reviewed · left: 12 due, 5 new, 1 learning"), the card as a card —
  its page and headings muted, the question as Markdown in a large size (`b.ui.markdown`: rich text
  through `InlineRenderer`, links open their pages), after **Show answer** (Space; a primary button
  that takes the row) a line and the answer —, then the four grades, Again (danger) · Hard (warning)
  · Good (success) · Easy (info), each with the interval it would schedule under its label ("10
  min", "4 dni") and its key 1–4 beside it, sharing the row evenly; under them **Undo** (U: the
  last grade's lines written back, the card shown again with its answer), **Edit** (the note at the
  card's line), the card's state and when it is due, and a menu (Start over, Open the page, Decks).
  Done: "All done for today" with the next due day, Study ahead (the next day's reviews), N more new
  cards, back to the decks.
* **Statistics**: tiles (due, reviewed today, retention of the last 30 days — success when within 5
  points of the desired one —, all; new, learning, young, mature), a bar chart of the grades per day
  (recalled, forgotten) and one of the cards due in the next 30 days, Study and Decks.
* **Keys** are the view's buttons' own (`b.ui.button`'s `key`): `ShellExtensionView` turns a key
  press into a name ("space", "enter", "1", "u", "ctrl+z") and `ExtensionView.activateKey` taps the
  first button that has it, unless a field took the key; the keys show as captions beside the
  labels where there is a keyboard.
* **Writing**: a grade writes the card's line (or Logseq's properties) and the month's review log
  through the extension (docs/SYNTAX.md §15); a line changed meanwhile is not overwritten ("The line
  changed in the meantime", the card shown again). The view is drawn again when notes change, so a
  grade made on another device (a sync) shows.

## Extension views

`ShellExtensionView.qml` over `ExtensionView` (`src/app/shell/extensionviewmodel.h`), a list model of
the node tree a view returns — docs/EXTENSIONS.md "Views" —, draws the gym, the habits and every
extension page's views. Its lines: headings; texts, with an icon before them (`lineIcon`, icon
size) and a tone (`lineTone`: success, danger or accent text, else muted or plain); empty states
with their own icon (else the dumbbell); buttons that open another view of the extension (`open`)
or run one of its commands (`run`); links — a link's `line` opens its page there
(`pageRequested(page, line)`, 0-based); menus as a "⋯" `BIconButton` ("More" without a label);
rows as cards of wrapping controls (all centred on one control height: the touch target on touch
UIs) — a check is the tasks' `TaskBox` (done, open, or skipped in a cancelled task's look), its
menu on a right-click or a long press (`controlHasMenu`; its label is its accessible name, "Reading:
done") —; month grids; charts and tables (the second column widest, numbers right-aligned); heat maps
(`HeatLine`: the model's `heatmapHead` and `heatmapRow` lines, as a table's — the label column, a
cell per column of at most 22 px (26 on touch UIs) with a gap of 3, tinted by its level 0–4 in
`bgSunken` or the accent at 22, 45, 70 and 100 %, the note column right after the cells; the head
writes every so many columns' names when the cells are narrow, the last always; a cell's text in
a `BToolTip` on hover or a tap, its accessible name); **pictures** (`pictures`: `lineItems` — src,
alt, caption, web, host, state, url, error —, `lineTitle` a caption above them) side by side, two
to a line on a phone and up to four on a wider one, each a 3:2 box in `bgSunken` the picture fits
in (the line keeps its height while they load), its caption under it, `alt` its accessible name: a
notebook's picture from its file (`Workspace.assetUrl`: only a picture file the index knows, else
"No picture “…” in the notebook"), one from the internet as a note's (`Workspace.webPicture`, the
model following `webPictureChanged`): ready from the cache, a spinner while it loads, "Picture
from <host>" while it waits, the error when it failed — and one **Show the pictures** (**Try
again** after a failure) under the line for them all (`ExtensionView.showPictures(row)` →
`loadWebPicture`).
**Cards** are no item of their own: the lines of a `card` node stay lines of the flat list (a
delegate each, recycled as any), each drawing its part — `lineCard` "first", "middle", "last" or
"only": the top with its corners, the sides, the bottom — so a card of a hundred lines scrolls like
a hundred lines. A **month grid** (`lineMonth`, `lineDays`: a map per day — date, day, state,
tappable, menu) is cells of 26 px (34 on touch UIs) with a gap of 4, under the weekdays' short
names from the notebook's first day (`DatesHelper.weekStart`); done in `successBg`, missed in
`dangerBg`, skipped in `chipDoneBg` with its number struck out, future and outside faint, a due
day with a 1-px ring and today with a 2-px one; a tap is the day's action (`activateDay`), a
right-click or a long press its menu. **Markdown** lines (`lineHtml`: the text's blocks rendered by
`InlineRenderer` with the model's `palette`, which ShellExtensionView binds to the theme's colours,
links opened through `PageTools`; `lineTitle` its plain
text for screen readers) in `BRichText`, larger or smaller by `lineSize`; **stats** tiles (`lineItems`: label, value, tone; four to a line,
two on a phone); a **divider** a hairline. **Buttons** with a key, a tone, a second line (`hint`)
or `primary` — and `open`s drawn as buttons (`lineStyled`) — are `ViewButton`s: tinted by their
tone, the key's caption beside the label (not on touch screens), a primary one alone in a row
taking the row, buttons with a second line sharing it evenly (four to a line). Menus — of a menu line, of a check, of a day — are one
`BMenu` (`menuLabels` / `menuIcons` / `activateMenuItem`): a view's action, a page at its line
(`file-text`) or a day's journal (`book-open`; `journalRequested`).

**Controls that take what is typed or chosen** (in a row, or a line of their own: a row of one
control, `lineBare`, without the row's card and as wide as the line): a text field (`input`: a
`BTextField`, or a `BTextArea` with `multiline`, its label above it and a primary button beside it;
Enter — Ctrl+Enter in a text area —, Done or the button call `ExtensionView.submitText()`, the field
is emptied unless `keep`, and it keeps what is typed while the view is drawn again in place); a
select (`select`: a small button with the chosen option and a chevron whose menu — the view's one
`BMenu` — lists the options, the chosen one ticked: its items are the select's action with each
option's value); a slider (`slider`: `BSlider`, the value with its unit beside the track; its
action once a drag ends or a key moved it — `ExtensionView.snapped()` puts the value on the step's
grid); a date (`date`: a button with the day as `DatesHelper.formatShort` writes it, else "No
date", that opens one `BDialog` with a `BDatePicker`, "No date" and "Choose"); a switch (`toggle`:
`BSwitch` with `checkable: false` — a tap asks for the other state and the next drawing shows what
the script says); a progress bar (`progress`, both forms of `b.ui.progress`: `BProgressBar` 6 px thick under its
caption, in the accent or the node's tone; on a line of its own — the flashcards' session — just
the caption and the bar across the line, in a row 200 px wide and as tall as its controls). Keyboard: Tab reaches every one; Space toggles and opens; arrows, Page Up / Page Down,
Home / End move a slider; touch targets are `Theme.touchTarget` high on touch UIs.

**Embedded** (`ShellExtensionView { embedded: true }`: a ```` ```view ```` block's, a side panel
section's): no scrolling of its own (`interactive: false`, the wheel and flicks go to the page), no
scroll bar, no margins, as tall as its lines (`height: contentHeight`, every line made at once —
a view of up to 2000 lines). Its `ExtensionView` is `live` only while it is on screen: a change
meanwhile marks it stale and it draws when it is live again.

A view renders again after every command and action of the extension and when the index changes (coalesced), never while a question waits; after an action
of its own controls at once. The same view drawn again with lines of the same kinds changes them in
place (dataChanged with the roles that changed; a row whose controls changed their kinds resets
only that row's model, from the timer — never inside the handler that acted): the delegates stay.
A **stepper** is − number +, its number a borderless `BTextField` framed by the stepper's box (the
field's hover and focus colours on the box): a tap or Tab selects the number, the touch keyboard is
numeric (`ImhFormattedNumbersOnly`); Enter (Done) or leaving the field writes what was typed through
`ExtensionView.enterValue` — a number of at least 0 with a comma or a point, the unit allowed after
it, different from the one shown — and anything else shows the number again, as Escape does; ↑ / ↓
step. Leaving it for another control writes first: the focus moves on press with a mouse, on
release on touch screens, before that control's click, which then acts on the line as written
(the view is in place by then). A field that loses the focus because its delegate goes while the
lines change writes nothing (`enterValue` is refused during the update).

**Questions** (`ShellExtensionQuestion.qml`, in AppShell): `b.ask` a text field, `b.choose` the
options as rows (one tap answers), `b.form` labelled fields — numbers with − / + and typing (`82,5`,
`82.5`, `82,5 kg`; − / + from a number between two steps go to the next step's value) and text
fields (`kind: "text"`: the label above a `BTextField` with the field's `value` and its
`placeholder`; the first one has the focus, Enter answers — `ExtensionFormModel.firstTextField`,
`setText`). A question that follows at once reopens the dialog (the habits' form asked again);
cancelling answers "" or nil, and only the question the dialog showed (`questionSerial`).

**Messages**: a command's or an action's result is a toast — a text as information, a table
`{message, tone, button}` in its tone (success, warning, error) and with its button
(`WorkspaceExtensions::messageWithButton` → `AppController.toastWithAction`; a press is
`pressButton(id)`: the view's action again — the habits' Undo —, a command with `ctx.args`, or a
page opened, made when missing; a toast that goes forgets its button).

**The palette** lists the extensions' commands with their own labels ("Siłownia: dodaj serię");
those on the block under the caret (`meta.block`) are enabled in an editor and run there (uiAction
`extension:<id>/<name>` → `BlockEditor.runExtensionCommand`: `PageController.savedLineAt(row)` saves
first, so the extension reads the line as shown).

### Extension items in the UI (`b.app.contribute`)

An extension with the permission `app` — and live code an AI agent loads (docs/EXTENSIONS.md "Live
code") — puts items into the app's own places (docs/EXTENSIONS.md "Items of the UI"). Every place
reads `Workspace.extensions.contributions(slot, Workspace.extensions.appRevision)`: rows {extension,
title, slot, id, label, icon, tooltip, tone, command, view, params, page, run, insert, key, live},
every key always there; a change of any extension's items (or of an agent's) bumps `appRevision`,
so an item comes, changes and goes at once, also in a menu that is open. Each place binds a string
to `contributionsKey(slot, appRevision)` (the slot's rows as compact JSON) and its model to
`contributions(slot, key.length)`: a QML string property notifies only when its value changes, so a
status bar text that counts on every change of a note does not make the top bar's buttons, the
menus' items or every editor's slash list again. A click, a tap or Space (Enter in a menu) runs
the item through `AppScripting.activate(slot, extension, id, path, line, context)`: its command (on a page:
`ctx.page`; on a block: `ctx.block`; the folder tree's row in `context` as `ctx.path` / `ctx.folder`;
a share as `ctx.shared`), view, page or palette command. A delegate reads
the row's `id` and `icon` through `modelData` (`UiModels.stringField`): a delegate's property cannot
have those names. Where each slot shows:

* `toolbar` — `ShellTopBar.qml`: an icon button (`toolbar_<id>`, a `BIconButton`) before More, the
  item's icon (else `puzzle`) tinted by its tone, its label as the accessible name and its tooltip
  (`BIconButton.toolTip`) on hover; a click leaves the keyboard focus where it was (`Qt.TabFocus`:
  the edited block keeps it, and a focused button that goes with the extension's next change would
  leave the focus nowhere — a dialog closed later gave it to a hidden view's editor); on narrow
  windows the first items of More
  (`contribution_toolbar_<id>`), above a separator.
* `statusbar` — `ShellStatusBar.qml` (wide windows), before the palette's key: the icon and the
  label in the tone's text colour (`successText`, `warningText` …, else `fgMuted`), at most a quarter
  of the bar (elided); an item that does something is a button (`statusBar_<id>`: hover, a pointing
  hand, a focus ring inside it, Tab and Space; a click keeps the focus where it was, as the top bar's),
  one that does not a plain text; a tooltip that says
  more than the label on hover.
* `sidebar` — `ShellRail.qml`: rail buttons after the app's own entries (`sidebarNav_ext_<id>`), so
  on phones in the drawer's rail too; one that opens a view is selected while that view shows; a tap
  in the drawer closes it.
* `pageMenu` — the top bar's More while a page shows (after "Open in another app", before the
  trash) and the page header's ⋯ (`PageView.qml`, its own section before encryption):
  `ShellContributionItems`, a `BMenuItem` each (`contribution_pageMenu_<id>`), the extension's
  title as the muted hint on the right (not on touch UIs); run with the page's path.
* `blockMenu` — `BlockMenu.qml`: a section before Indent / Move, run on the block's line as saved
  (`PageController.savedLineAt(row)`, as the palette's block commands); not in reading mode (a
  command may change the note).
* `slash` — the editor's `/` menu (`SuggestionModel.extensionItems`, bound in `BlockEditor.qml`):
  found by its label as the commands are by their names — at its start, at a word's start,
  anywhere; the extension's title as a keyword — and ranked among them (after the commands that
  match as well), the extension's title as the subtitle, the icon or `puzzle`. Accepting one is
  `PageController.applySlashContribution()` (command `ext:<extension>/<id>`): its Markdown (`insert`)
  replaces the typed "/…" — one line at the caret, as /today writes its link; blocks in place of
  the block when nothing else is in it, else after it at its depth (`insertMarkdown` /
  `insertAfterBlock`), the caret at the end of the last one — in one undo step; an item that runs a
  command removes "/…" and the editor runs it on the block (`{action: "extension"}` →
  `BlockEditor.runSlashItem`).
* `tabMenu` — `ShellTabBar.qml`'s tab menu, after Move to new window and a separator
  (`contribution_tabMenu_<id>`, `ShellContributionItems`): run with the tab's route's page
  (`AppController.tabRoute(id).path`, "" for a tab that shows no page).
* `folderMenu` — `ShellSidebar.qml`'s row menu (`sidebarContextMenu`), a section after Page history,
  before Move to trash (`contribution_folderMenu_<id>`): `ShellSidebar.runTreeItem()` runs it with
  the row — a folder as `{path, folder}` and no page, a page as its path (`ctx.page`) and
  `{path, folder: ""}`, another file as `{path, folder: ""}` only (the map kept in a property:
  built inside the call, compiled code passed it empty).
* `bottomNav` — `ShellBottomNav.qml`: `NavTab`s after the views (`contribution_bottomNav_<id>`), the
  icon, the label as the name, current (the accent pill with the label) while the view the item opens
  shows. At most two (appapi.h `MaxBottomNavItems`), and only as many as leave every item of the
  capsule its 48 px (`extensionsShown`): the views are never squeezed out; the current pill's width
  counts them (`itemCount`). Run with the route's page.
* `keyboardToolbar` — `MobileEditToolbar.qml`'s block tools, after the insert tools (voice note) and
  a separator, before Undo (`contribution_keyboardToolbar_<id>`, a `ToolButton`: no focus, the
  label as its name, the tooltip on a long press): `BlockEditor.runToolbarItem()` commits the
  composed word and runs it with the selection (`selectionForExtensions()`, `{selection}` → `offerSelection`) on
  the edited block's saved line. Not in the cell's or the source's tools.
* `share` — `ShellShareActions.qml` (`contribution_share_<id>`, a `BButton` each under "With an
  extension"): in `ShellShareDialog.qml` below its hints — run with the dialog's text as edited
  (`{text}` → `AppController.sharedForExtensions()`), then the share is dismissed (nothing saved) and
  the dialog closes — and in `ContentExportDialog.qml` before "More export options…" — run with the
  page's path (AppScripting reads it as `ctx.shared`), the dialog closes.

Menus keep BMenu's rules: the items are plain `BMenuItem`s inserted after a marker item
(`ShellContributionItems.after`) — the arrows reach them, Enter runs them —, never `danger` (what
deletes stays the app's own last item, after its separator).

**Live changes.** While a live client — an AI agent over MCP or ACP, `brulion-cli app …`, an
extension's `b.app.set` — has changed the running app and not been undone
(`AppScripting.liveClients`: [{who, name, changes}]), the top bar shows a **Live** chip
(`ShellLiveIndicator.qml`, `topBarLive`) after the title, on phones too: an accent pill (`accentBg`,
an `accent` hairline) with a bolt (`zap`) and "Live" — on narrow windows the bolt alone —, its
tooltip "Changed live by Claude Code (3 changes)" — an agent on another computer by its name there,
"Changed live by Claude Code on raspberry-pi-4" — (a long press on touch screens), its accessible
name "Live changes" with the same description, a focus ring, a touch target. A click, a tap or
Space opens its menu (`liveMenu`): a row per client for reading ("Claude Code — 3 changes",
`liveClient`), "Undo changes of …" per client when there are two or more (`liveUndoClient`,
`AppScripting.revert(who)`), "Undo all live changes" (`liveUndoAll`, `revert("")`) and "Open the
console" (route `console`). Undoing puts settings back, takes live themes away and unloads the live
code; the chip goes with the last change.


## Extensions

Settings → **Extensions** (`SettingsView.qml`, docs/EXTENSIONS.md): the switches of the gym (off by
default) and of the habits (`habitsSetting`, on by default), each with its code's row while on —
**The gym's code**, **The habits' code** (`BuiltinSourceRow`, one component for both): "Edit the
code" writes its source into a page of the notebook (`Rozszerzenia/Siłownia.md`,
`Rozszerzenia/Nawyki.md`; `Extensions/…` in English) and opens it, after which the built-in runs
from that page ("Open" then); "Restore" puts the page in the trash. **In
this notebook**: "New extension" (a page in `Rozszerzenia/` with an example — a command, a view, a
hook — approved on this device, opened), "Console", and a row per extension page
(`Workspace.extensions.list`): its title, its state ("Runs: 2 commands, 1 view, 1 reaction", "Error
on line 14: …", "New or changed elsewhere: read the code, then run it on this device.", "Off on
this device."), what it may do, the pages of shared code it uses ("Uses the code of: …"), the type
checker's warnings ("Type check: 2 warnings, line 14: …") and its description; "Code" (or "Line
14") opens the page (at the line of the error), "Read and run" opens the approval (below),
"Settings" (`extensionSettingsButton`, when it declares some) its settings, the copy button
(`extensionCopyButton`) "Copy to another notebook", "Switch off" / "Switch on". Live code an agent
loaded for this run (`extKind` "live", docs/EXTENSIONS.md "Live code") is a row too
(`extensionLiveRow`): a **Live** badge (`extensionLiveBadge`), its state, "From Claude Code — for
this run of the app only" (its description is who sent it) and **Remove**
(`extensionLiveRemove`: `Workspace.extensions.unloadLive(id)`); no Code, copy or switch. A page (or
built-in) a live version shadows says "A live version runs instead (sent by an agent)." (state
`live`). The built-ins' code
rows have "Settings" too (`builtinSettings`: the gym's rest between sets), and the AI group has
**The assistant's code** (`assistantSourceSetting`).

An extension page shows its state in its bottom-left corner (`pageExtensionState` in
`PageView.qml`): "Extension: runs" (with the type checker's first warning and a button to its line,
`pageExtensionTypeLine`), the error with its line and a button to it, "new or changed elsewhere —
read it, then run it" with "Read and run" (`pageExtensionApprove`), "off on this device" with
"Switch on"; a settings button (`pageExtensionSettings`) when it declares settings; the console's
button. It follows every save: the page's code loads again as it is saved.

**The approval** (`ShellExtensionApproval.qml`, uiAction `approveExtension:<path>`, made by the
shell): the title "Run “…”?", what changed in words (new on this device; replaces the built-in …;
"3 lines added, 1 removed" since the version approved here), badges of what it may do — a
permission it did not ask for before in the warning colour, with a sentence —, the code against the
version approved before in `BDiffView` (side by side when wide, one below the other on a phone),
"Run this version". A sync that changes the code while it is open shows a warning band ("The code
changed while you read it … Nothing was approved") with "Show the new version", and "Run this
version" stays disabled until it is shown: only the version on screen is ever approved
(`approveVersion(path, hash)`).

**An extension's settings** (`ShellExtensionSettings.qml`, uiAction `extensionSettings:<id>`): a
row per declared setting (`extensionSetting`) — a number with − / + and a field taking `82,5`, a
text field, a switch, chips of a choice —, each kept at once; "Defaults" puts them back. **Copy to
another notebook** (`ShellExtensionCopy.qml`, uiAction `copyExtension:<path>`): the recent notebooks
but this one and the documentation (`extensionCopyTarget`); a page there with other text asks
"Replace" (danger) first.

**Timers** (`ShellTimerBanner.qml`, under the top bar in every view, below the update banner): a
line per extension's timer (`Workspace.extensions.timers`) — its label and extension, the time
left in monospace, a bar, × (`shellTimerStop`) — while one runs; hidden while typing and in focus
mode; in the Gym view the gym's own rest stays in the Gym's banner and this one shows the others.

**Luau console** (`qml/views/ConsoleView.qml`, route `console`, the palette's "Luau console", icon
`terminal`): a line of Luau and "Run" (Enter); Up / Down recall what was typed; the lines above —
the input (`> …`), values, `print()` and `b.log` of extensions with the extension's name, errors in
red — "Copy everything", "Clear". **Run Luau block** (the palette, in an editor): the
```` ```luau ```` block under the caret runs in the console; its value or error shows as a toast.

**An extension's view** (`qml/views/ExtensionRouteView.qml`, route `extension` {extension, name,
title, …params}): a view registered with a label (`b.view(name, render, actions, {label, icon})`)
is a palette command ("extview.<extension>.<name>"); the view is drawn by `ShellExtensionView` like
the gym's, its label is the title (kept in the route for tabs); its "open" nodes navigate to
another view of the extension with their params.

**AI** (Settings → Extensions, group "AI", `objectName` `aiSettings`; docs/EXTENSIONS.md "AI"):
the AI assistant's switch (`AppSettings.assistantEnabled`), the provider (Claude / OpenAI-compatible
/ On this device: `aiProvider`; the third only where `AppController.localAi.available`, a build
with the model runner), the API key (a password field; saved when editing finishes, then emptied:
"Saved on this device", "Forget"), the model and the address (`aiModel`, `aiBaseUrl`; placeholders
show the defaults), "Check" (`Workspace.extensions.testAi()`: the model's "OK" or the error as a
toast; busy while `aiPending`). The group's note says what it costs and where the notes go (and,
where AI agents run, that a subscription works with one). Under it the group **AI agents**
(`acpAgentsSettings`, docs "AI agents (ACP)" below), and on the desktop the group **claude.ai and
ChatGPT** (`mcpServerSettings`, `AppController.mcpServer`: `src/app/shell/mcpserversupervisor.h`;
docs/CLI.md "Remote MCP server", "From the app"): the remote MCP server run in the background while
the app runs — **Connector server** (`mcpServerSetting` → `AppSettings.mcpServerEnabled`; its
description is the state: off, starting, running, "Not running: <the server's reason>", "Stopped:
… Starting it again…"), **Connector URL** while it runs (`mcpServerAddressRow`: the endpoint in
mono, `mcpServerEndpoint`, **Copy** — `mcpServerCopy`, a toast —, and where to paste it, or that
only this computer reaches a plain http one), **Password** (`mcpServerPassword`, a password field
saved when editing finishes, then emptied: `AppSettings.setMcpServerPassword`, the field's error
below 8 characters), **Public address** (`mcpServerPublicUrl`), **Port** (`mcpServerPort`, 1024 …
65535), **Only reading** (`mcpServerReadOnly`), and **Requests** (`mcpServerLogRow`: Show / Hide the
server's last lines, `mcpServerLog`).
With **On this device** the key, model and address rows give way to the model's own
(`AppController.localAi`, `src/app/shell/localai.h` — typed `FINAL` properties about the chosen
model, so the bindings compile): **Model** (`aiLocalModelSetting`: Brulion / LFM3 1.7B /
LFM3-VL 2B, the best with the assistant first and the default → `AppSettings.aiLocalModel`; a model's `label` where its name is long — Brulion
Assistant 1.7B's —, so the three fit side by side on a 360-px phone; the description says what
the chosen one is good at: it sees pictures, it reads text only, or — `modelAdapted` — it is
`modelBase` by `modelBaseMaker` trained further for the assistant, answering at once — and of a
model that reads text only, a second line: which model looks at pictures in its place
(`pictureModel`) — loaded for them when it is on the device too (`pictureModelHere`), else to
download as well), **The
model's files** (`aiLocalFiles`: not here yet with its size and **Download** — partly here, a file
another model shares or a download that stopped, with what is left of the whole (`modelLeft`), or
"nothing left to download" when another model brought all its files; while downloading the
progress as text, a progress bar and **Stop**; "Checking the downloaded files…"; a failed download
with its reason — it goes on from where it stopped; here: its size and **Remove**; a second line
names the models it shares a file with, `modelSharesWith`) and **Whose model** (the maker and
the licence, with links; of an adapted model the base model's maker and licence, and that
Brulion trained it further, a version that maker neither made nor endorses). One model downloads
at a time (`downloadingOther` disables Download on the other). Under them **A model of your own**
(`aiOwnModels`; docs/EXTENSIONS.md "AI"): **Add a model file…** (`aiAddModelFile`: a `FilePicker`,
`*.gguf` — on Android any document, copied: `copiesFiles`) → `LocalAi.addModelFile`; while it is
read, copied and loaded once, a line with what happens (`addText`: "Copying Qwen3-4B.gguf: 1.1 GB of
2.5 GB", "Checking that … loads…"), a progress bar (`addProgress`, indeterminate while not known)
and **Stop** (`aiOwnAddingStop`); a refused file "Not added: <the reason>" in the danger colour
(`addError`); then a row per model of one's own (`aiOwnModelRow`: its name, its details — "Qwen3
4B · Q4_K_M · 2.5 GB · context 32 768 · ChatML", or that its file is gone — and **Use**, or "In use"
for the chosen one). A model added is chosen (AppController: `ownModelAdded`). With one chosen, the
**Model** row describes it (what it is, `memoryNote` where it takes more than half of the device's
memory), **The model's files** says where its file is (used where it is, or Brulion's copy), whether
it sees, with **Pictures…** (`aiLocalPictures`: its vision projector, a second picker) / **No
pictures** (`aiLocalNoPictures`) and **Remove**; **Name** (`aiOwnModelName`, a field:
`renameModel`) replaces **Whose model**. The rows' long descriptions are small typed functions of
the group (`localAbout()`, `filesAbout()` …), not nested conditionals (compiled QML). While a model is
downloaded, a file copied or a model answers (`working`) a phone's screen stays on, and Android's
foreground service runs with its notification (`LocalAi::notice()`; docs/ANDROID.md "A model on the
phone"). A command whose
extension asks AI shows "Asking AI…" (once per call: an agent asks many times); its result comes
as the command's own message when the answer arrives; the console shows "Asking AI…" and then the
value. The assistant is talked to in the AI chat ("AI chat" below): its command `chat` is no
palette command. A question's lines after its first show under the dialog's title as plain text
(`ShellExtensionQuestion`, `extensionQuestionDetails`: what a change would write); a question of a
chat's turn is asked in the conversation instead while a chat view shows it. While the assistant
is on, the top bar has its ✨ button (`ShellTopBar`, `topBarAi`, on phones too): a menu
(`topBarAiMenu`) of "New conversation" (`aiAsk`: about the page on screen — in the journal's stream
today's note when it exists), "New conversation beside the page" (`aiAskBeside`: in the side panel;
wide windows with a page), "Conversations" (`aiChats`), "Summarise this page" (`aiSummarize`: a new
conversation that asks for it at once; disabled without a page) and "AI settings…"; a spinner in
its place while a model answers (`aiPending`) — turning in steps ten times a second
(`BSpinner.calm`), and none while a chat view shows the turn (`Workspace.chats.runningShown`: the
chat shows what the AI does; every frame of a spinner is the whole window drawn again, processor
time a model on the device loses).

The palette also has "New extension", "Reload extensions" (a fresh VM, every extension loaded
again) and "Extensions" (Settings → Extensions). Extension commands carry the extension's title as
their subtitle and the icon `puzzle` (the gym's `dumbbell`, the habits' `flame`); the views of the
gym and the habits are offered as "Gym" and "Habits" (`go.gym`, `go.habits`, while they are on),
not as `extview.…`.
## Code mode

Projects and AI agents' threads, as in T3 Code (docs/CODE.md: the model; this section: the UI). The
mode (`AppSettings.appMode`: "notes" | "code") switches on the rail's first button (`modeSwitch`: a
`code` icon in Notes mode, `notebook-pen` in Code mode; "Code mode" / "Notes mode", in the drawer
"Code" / "Notes"), the palette's `mode.code` / `mode.notes` and Settings → Extensions → Code mode
(`codeSettings`: the mode `appModeSetting`, "Open in" `codePreferredEditorSetting` — the editors found,
the custom command, the file manager —, the custom command `codeEditorSetting`, "New threads work"
`codeNewThreadEnvSetting` (As t3.json says / Local / New worktree), the branch prefix
`codeBranchPrefixSetting`, "Worktrees of deleted threads" `codeDeletedWorktreesSetting` (Ask / Delete /
Keep), the worktrees' folder `codeWorktreesSetting`, "Draw terminals with Atrament"
`codeTerminalAtramentSetting` (`AppSettings.terminalAtrament`, experimental, where Atrament is built:
docs/CODE-TERMINAL.md "Drawn by Atrament"); shown where agents run).

* **In Code mode** the rail shows only "Code" (`sidebarNav_code`, route `code`; `ShellRail.shows()`), the
  page panel is `CodeSidebar` (AppShell's loaders choose it by the mode, wide and in the drawer) and the
  bottom navigation is hidden; settings, notifications, the palette, tabs and the top bar stay. Ctrl+1–9
  open the sidebar's threads instead of the tabs (Ctrl+PgUp / PgDown still switch tabs), and Code mode's
  commands have shortcuts of their own (docs/CODE.md "Shortcuts": ShortcutMap's scope `code`; AppShell's
  Shortcut of a command that is everywhere is off in Code mode while a Code mode command has its key).
* **CodeSidebar** (`qml/code/CodeSidebar.qml`): "Projects" with the threads' order (`codeThreadSort`,
  `codeThreadSortMenu`: Last activity first `codeSortActivity` / Newest first `codeSortCreated`;
  CodeProjects.threadSort, this device's) and "Add a project…" (`codeAddProject`, a menu
  `codeAddProjectMenu`: "Open a folder…" `codeAddFolder` — the folder picker `codeProjectFolderDialog` —
  and "Clone a repository…" `codeAddClone` — `CodeCloneDialog`, docs/CODE.md "Cloning a repository";
  with none: a note, `codeAddProjectEmpty` and `codeCloneEmpty`), the clones running or failed
  (`CodeCloneRow` `codeCloneRow`: the name, `codeCloneStatus`, a bar, `codeCloneRetry` after a failure,
  `codeCloneCancel`), a filter over the threads'
  titles and branches (`codeThreadFilter`: ChatListModel.filter), then a section per project
  (`CodeProjectSection`; pinned projects first): its row (`codeProject_<id>`: a click folds it — kept in
  `code/collapsed` —, its icon `codeProjectIcon` — the user's emoji, t3.json's iconPath picture, a
  folder —, a pin, "+" `codeNewThread_<id>`, "⋯" `codeProjectMore_<id>`; dragged with the mouse
  (`codeProjectDrag`, a line `codeProjectDrop` where it goes) to another place within the pinned ones or
  the others), the project's menu (`codeProjectMenu`, also a right click / long press: New thread
  `codeProjectNewThread`, New thread in the project's folder `codeProjectNewLocalThread`, Open in editor,
  Show the folder, Copy path, Pin / Unpin `codeProjectPin`, Move up / Move down, Project settings…
  `codeProjectSettings`, Remove from the list `codeProjectRemove` — confirmed: its folder and threads
  stay), a draft of a new thread when one was written and not sent (`codeDraft_<id>`, ChatView's draft
  "code:<id>"), its threads (`CodeThreadRow`, `codeThread`, `ChatListModel { project: id }` with the
  filter and the order) and "No threads yet" / "No thread matches". **A thread's row**: its state by
  priority (`codeThreadState`: a plan to approve — `list-checks` and "Plan" —, an approval asked for —
  `circle-help` and "Approve" —, a spinner while it works, the alert and "Failed" in the danger colour, an
  accent dot while its answer is unread), the title (semibold unread or current), a pin, `terminal`
  while a program runs in its terminal (`codeThreadTerminalBusy`: TerminalRegistry.isBusy, T3 Code's
  "Terminal process running"), `git-branch` for a worktree of its own (its branch in a tooltip), when; the current one selected; "⋯"
  `codeThreadMore` on hover, a right click / long press: Rename… (`codeThreadRename`: a dialog
  `codeRenameThreadDialog` / `codeRenameThreadField`), Regenerate title (`codeThreadRegenerateTitle`,
  while the AI of Settings → Extensions → AI is set up: T3 Code's — WorkspaceChats.regenerateTitle, the
  AI names the thread anew from its messages; "Regenerating…" meanwhile; the thread shown follows its
  new name, "Title kept" and a failure are toasts), Pin / Unpin, Mark unread, New thread in this
  worktree (or in the project's folder) `codeThreadNewHere`, Open in editor, Copy path / Copy branch /
  Copy link, Archive / Unarchive, Move to trash (`codeThreadDelete`). A thread with a worktree of its own
  that no other thread uses (`WorkspaceChats.threadsIn`) asks first (`codeDeleteThreadDialog`, unless
  "Worktrees of deleted threads" says Keep — or Delete and the worktree has no uncommitted changes):
  where the worktree is, that its branch stays, how many files have uncommitted changes
  (`CodeProjects.checkWorktree`), "Delete the worktree too" (checked when nothing would be lost). The toast's
  Undo brings the thread back, and the worktree from its branch when it went (`CodeProjects.keepUndo`).
  Archiving keeps the worktree.
* **Clone a repository…** (`qml/code/CodeCloneDialog.qml`, in CodeSidebar and CodeView: `start()`):
  first `CodeClonePicker` (`codeClonePicker`, a BPalette: the user's repositories on GitHub, "Clone
  “x”" for what is typed, why it is no address as its empty text), then a BDialog (`codeCloneDialog`:
  the repository as its title, its address as the subtitle, the folder `codeCloneFolder` with
  "Choose…" `codeCloneChooseFolder` — a folder picker `codeCloneFolderPicker` —, while it clones a bar
  `codeCloneProgress` and git's stage `codeCloneStatus`, an error `codeCloneError`). Clone starts it;
  closing the dialog ("Hide" while it clones) leaves it running. Done while shown: `added(project)` —
  a new thread of it; hidden: a toast.
* **CodeView** (`qml/code/CodeView.qml`, route `code`: `{}` a new thread of the last project used —
  `code/lastProject` — or the first; `{new: true, project, env?}`; `{new: true, project, worktree, branch,
  pr?}` a new thread in that worktree (on that pull request); `{path}` a thread; `{addProject: true}` the
  folder picker; `{cloneRepository: true}` "Clone a repository…"): no project yet — an empty state
  (`codeEmpty`) with "Add a project…", "Clone a repository…" (`codeEmptyClone`) and, without an
  agent, "Set up an AI agent". A header (`codeHeader`): the project (`codeProjectChip`; for a new thread a
  menu of the projects, `codeProjectPick`, "Add a project…" and "Clone a repository…"), the thread's name (`CodeThreadTitle`,
  `codeThreadTitle`: a double click or F2 makes it a field `codeThreadTitleField`, Enter renames the
  thread — WorkspaceChats.rename —, Esc keeps it), then where it works — **a thread**: its folder's branch
  (`codeWhere`: `git-branch` for a worktree of its own, `hard-drive` for the project's folder; the folder
  in its tooltip; a click opens the branch picker); **a new thread**: the environment (`codeEnvironment`,
  Ctrl+Shift+X: a menu `codeEnvironmentMenu` — Local `codeEnvLocal`, New worktree `codeEnvWorktree`, the
  project's worktrees `codeEnvExisting_<n>`; the default CodeProjects.newThreadEnv, the route's `env` or
  `worktree`), the base of a new worktree or the branch of a Local one (`codeBaseBranch`: "From main" /
  the branch; the branch picker) and the pull request it is on (`codePullRequestChip`) — the project's
  scripts (`codeScript_<n>` buttons with their icons, the first two of more than three and all of them in
  `codeScriptsMenuButton` / `codeScriptsMenu`: run in a new tab of the terminal), "Open in…"
  (`CodeOpenIn`: `codeOpenEditor` opens the folder in the preferred editor, Ctrl+O; `codeOpenIn` the
  menu `codeOpenInMenu` of the editors, `codeOpenIn_<id>`: the one picked opens it and is remembered),
  the terminal's and the changes' toggles (`codeTerminalToggle`, `codeChangesToggle` Ctrl+D; kept in
  `code/terminalOpen`, `code/changesOpen`). **The branch picker** (`CodeBranchPicker`,
  `codeBranchPicker`, a BPalette; Ctrl+Shift+G): the folder's branches with their badges in the rows'
  subtitles (current, default, remote, the worktree that has one, ↑ ahead ↓ behind), searched; typed text
  offers "Create branch “x”" for a new valid name and "Check out pull request #123" for `#123`, `123` or
  a pull request's address. Picking a branch checks it out — refused while an agent's turn of a thread of
  that folder runs (a toast says why; the picker's empty text too) —; a branch checked out in another
  worktree moves the thread there (a new thread: that worktree); for a new worktree it is the base.
  **"Check out pull request #N"** (`CodePullRequestDialog`, `codePullRequestDialog`: New worktree /
  Local `codePullRequestWhere`, its error `codePullRequestError`): a new thread then works there and
  its agent is told which pull request it is. Under the header, while a new worktree's setup scripts run
  and the message waits for them, `CodeSetupBanner` (`codeSetup`, `codeSetupText`: "Setting up the
  worktree… (Setup)", Send anyway `codeSetupSend`); one that failed says so, with Send anyway and Don't
  send (`codeSetupCancel`: the message stays in the field, the worktree stays). Under it a horizontal
  SplitView: the thread — `ChatView` with `routeView: "code"` (its navigations name route `code`),
  `showList: false`, `project`, `wantsWorktree` / `worktreeWanted()`, `newWorktree` / `newBranch` /
  `newPullRequest`; only agents among the partners (`chatNewPartner` shown with one agent too; the
  project's default agent chosen); its empty state "What should the agent do?" with suggestions
  (`codeSuggestions`) — over the terminal (`codeTerminal`, `TerminalPanel` — the thread's terminals, `TerminalRegistry.thread(path or the new
  thread's folder)`: tabs, splits, the find bar, the context menu with Add to chat into the thread's
  field, Ctrl+click on a file path opens it in the Files tab (another folder's in the editor); its commands `terminal.*` — Ctrl+J shows
  or hides it — are uiActions `terminal:…` CodeView carries out, docs/CODE-TERMINAL.md "Keys"), and the Changes panel
  beside them (`codeChanges`, `GitChangesPanel`:
  docs/CODE-GIT.md "The panel" — the diff sources Latest turn / Turn N / Uncommitted / Branch /
  Commits, `GitDiffView` with hunks, find and review comments, the file menu, the quick action, the
  merge / rebase banner). CodeView gives it the thread's `checkpoints` and turns its signals into the
  editor (`openFile`), the composer (`addToChat` → `chat.addToComposer`), the clipboard (`copyText`)
  and the browser (`openUrl`).
* **The right panel's tabs** (`codeRightPanel`, `CodePanelTabs`: `codeTabChanges`, `codeTabFiles`,
  `codeTabSearch`; kept in `code/rightTab`; docs/CODE-FILES.md): Changes (`codeChanges`), **Files**
  (`codeFiles`, `CodeFilesPanel`: a header `codeFilesHeader` — recent files `codeFilesRecent`, the path
  `codeFilesPath`, Go to file… `codeFilesGoTo`, Copy path `codeFilesCopyPath`, Open in editor
  `codeFilesOpenEditor`, Reload `codeFilesReload` —, `codeFilesTruncated` over a big file's start, the
  lines `codeFileView` / `codeFileLines` with rows `codeFileLine_<n>`, a picture `codeFilePicture`, a
  message `codeFilesMessage` for binary, missing, folders, outside; no file: `codeFilesEmpty` with
  `codeFilesEmptyGoTo` and `codeFilesRecentList`; lines selected: `codeFileSelectionBar` with
  `codeFileComment` → `codeFileCommentDialog` (`codeFileCommentField`, "Add to chat") and
  `codeFileCopyLines`, a right click `codeFileLineMenu`) and **Search** (`codeSearchPanel`,
  `CodeSearchPanel`: `codeSearchField`, toggles `codeSearchCase` / `codeSearchWord` / `codeSearchRegex`,
  `codeSearchStatus`, `codeSearchEngine`, `codeSearchStop`, results `codeSearchResults` with rows
  `codeSearchFile` and `codeSearchMatch`, `codeSearchEmpty`). The **file picker** (`codeFilePicker`,
  `CodeFilePicker`: `codeFilePickerField`, `codeFilePickerList` with `codeFilePickerRow`s): Print page's
  key (Ctrl+P) in Code mode, the palette's "Go to file…"; Search's key (Ctrl+Shift+F) and "Search in the
  project" open the Search tab (AppShell's `runShortcut()` → uiActions `code:findFile`, `code:search`).
  In a thread `@` offers the folder's files and folders (`chatMentions`, CodeFileIndex; Tab or Enter
  takes the first), and a link of an answer or a tool call's file (`chatToolFile`) to a file of the
  folder opens it in the Files tab at its line (ChatView's `filesFolder`, `fileLinkActivated(path,
  line)` → `CodeView.openFileInViewer(path, line)`).
* **Narrow** (CodeView's `narrow`: under 640 px — a phone, a narrow window): the header keeps the
  project, where the thread works and the toggles — no title (the top bar has it), no "Open in…", the
  scripts only in their menu (`codeScriptsMenuButton`) —; the changes' toggle (and Ctrl+D) shows the
  right panel in the thread's place and goes back to the thread, not kept (`remoteChangesShown`).
* **CodeProjectDialog** (`codeProjectDialog`): the name `codeProjectName`, the icon (an emoji,
  `codeProjectIconField`), new threads' agent (`codeProjectAgent`) and environment (`codeProjectEnv`:
  Default / Local / New worktree), and the scripts (`CodeScriptsEditor`, `codeProjectScripts`; rows of
  `CodeScriptsModel`): a row each (`codeScriptRow_<n>`) — its icon (`codeScriptIcon_<n>`, a menu of T3
  Code's six), name (`codeScriptName_<n>`, "Another script has this name"), command
  (`codeScriptCommand_<n>`, monospace), Move up / Move down / Delete (`codeScriptUp_<n>`,
  `codeScriptDown_<n>`, `codeScriptDelete_<n>`: not for a row of t3.json), its shortcut
  (`codeScriptShortcut_<n>`: a click records the next key — Esc cancels, Backspace none —, checked with
  ShortcutMap.checkIn), "Run when a worktree is made" (`codeScriptOnWorktree_<n>`), "Wait for it before
  the agent starts" (`codeScriptWait_<n>`), "from t3.json" / "overrides t3.json" chips — and "Add a
  script" (`codeScriptAdd`). Save keeps all of it (only this device's scripts: t3.json is never written).
* **Another computer's projects** (docs/CODE.md "Threads on the phone"; `CodeProjects`' remote ones, from
  `Workspace.projects.remote`): CodeSidebar lists them after this device's, a `laptop` icon and "on
  <computer>" (`codeProjectHost_<id>`) beside the name, no "⋯" (a long press: only New thread); it asks the computers for
  their lists again whenever it is shown. CodeView names the computer in the project's chip ("Sklep · nixos", a `laptop`
  icon), no terminal, editor or script buttons, and **CodeRemoteChanges** (`qml/code/CodeRemoteChanges.qml`,
  `codeRemoteChanges`) in place of the Changes panel — beside the thread when wide (`code/changesOpen`),
  under it when narrow, shown with the changes' toggle (not kept): "Changes and the terminal are on
  <computer>: here they are read-only." (`codeRemoteNote`), refresh (`codeRemoteRefresh`), This turn / All
  changes (`codeRemoteMode`, with the turn's checkpoint), the files (`codeRemoteChangeList`, rows
  `codeRemoteChange`: the letter in its colour, the name, its folder dimmed, +N −N) and a file's diff
  (`codeRemoteDiffPane`, `BDiffView`: beside the list when wide, in its place with Back when narrow) —
  what that computer's git says (`RemoteProjects.requestChanges` / `requestDiff`), refreshed when the
  thread's page changes. Local / New worktree work as for this device's projects: the worktree is made
  there.
* **What the desktop app there is doing** (docs/CODE.md "The computer's own turns on the phone";
  `ChatComputerList`, `qml/views/ChatComputerList.qml`, `chatComputers`): "On your computers" above the
  projects (and above ChatView's list of conversations) while a computer's app runs, waits on or asks
  something in a conversation, or ended one in the last 10 minutes — a row a conversation
  (`chatComputer_<name>`: a `laptop` icon with a spinner while it runs, `circle-help` while it asks,
  "Asks on nixos: …", "On nixos: Reading …", "Finished on nixos", "Failed on nixos"); a row opens it.
  Its view follows that turn (`ChatTurn.remoteHost`): "Running on nixos — answer, stop or write from
  here: your messages wait there." above the field (`chatOnComputer`, `chatOnComputerText`), its
  question's buttons, Stop and the field go there, and its waiting messages are that computer's
  ("waits on nixos", without Send now / Edit / Remove) — also those held there after its turn was
  stopped or failed, with Resume going there; pictures go with a message (prepared as for the model);
  the options picker above the field shows that app's agent's options and a choice goes there. A new thread of such a project starts in the
  app there: "Starting the thread on nixos…" (a spinner in `chatOnComputer`) until it is made; refused,
  its text goes back into the field and a toast says why.

## Notifications

What the app told the user is kept, per notebook and on this device (`shell/notificationcenter.h`,
QML `Workspace.notifications`, its device state `notify/log` — never in the notebook, it syncs
nothing): a reminder or a calendar event's alarm that rang (`WorkspaceReminders::fired`), the AI
chat's answers, failures and questions it notified of (`WorkspaceChats::notifyUser`), an extension's
timer that ended, and the notifications AI agents (`chatNotify`), extensions (`b.notify`) and the
user's scripts (`brulion-cli app chat notify`) send. Newest first, at most 200 and none older than 30
days; one that came while the app was in front counts as read, the others are **unread** until
opened from the list, until what they point to is opened another way (`markOpened`: a system
notification's click — `AppController::openNotification` —, the conversation shown in a chat view)
or "Mark all as read".

* **The bell** (`topBarNotifications`, the top bar before the AI button): `bell-ring` and the unread
  count as an accent badge (`topBarNotificationsBadge`, "9+") while something is unread; on narrow
  windows only then — **More → Notifications (3)** (`menuNotifications`) and the palette's
  "Notifications" (`go.notifications`) always lead there. Route `notifications` (views table).
* **Posted ones** (`NotificationCenter::post`) — an agent's, an extension's, a script's — are checked
  against the user's settings (Settings → Extensions → AI → "Let AI agents send notifications",
  `AppSettings.aiAgentNotify`; Settings → Extensions → "Let extensions send notifications",
  `extensionsNotifySetting`, `AppSettings.extensionsNotify`, key `extensions/notify`; a script of the
  user's always), at most **10 an hour from one source** (a conversation, an extension, the scripts:
  `MaxPerHour`, the workspace's clock), titled with who sends them ("Claude Code: Build", an
  extension's name), cleaned (no control characters but line ends and tabs, no runs of empty lines)
  and cut (1 000 characters, the title 80). Shown (`shown` → `workspaceintegration.cpp`): a system
  notification while the app is not in front — its id `brulion-message:<kind>:<what it opens>`, a
  newer one of the same replacing it —, else, or with nowhere to show one, a toast with **Open**
  (`AppController::toastOpen`). The app lock's "Hide the note's text in notifications" makes it
  "Brulion" — "Something new: open Brulion to see it".

## AI chat

`qml/views/ChatView.qml`, route `chat` (the palette's "AI chat", `go.chat`, and "AI: new conversation",
`ai.newChat`, while the assistant is on; the ✨ menu, "Top bar" and "Extensions" above; icon
`message-square`): conversations with the AI, as in a messenger. Each one is a page in `AI/`
(docs/SYNTAX.md §14), named after its first question and linked from that day's journal; the
assistant's agent answers (docs/EXTENSIONS.md "AI") — or an extension's chat command chosen for the
conversation (docs/EXTENSIONS.md "Chat partners"), or an AI agent of the user's own ("AI agents
(ACP)" below).

* **Routes**: `{view: "chat"}` the conversations; `{view: "chat", path}` one; `{view: "chat", path, side}` one
  with its side chat `side` open (below; a side chat's own path as `path` opens so too — a toast's Open, a link
  to its page); `{view: "chat", new:
  true, page?, prompt?}` a new one about `page` (a page name as a link names it), `prompt` sent at
  once ("Summarise this page"); `{view: "chat", new: true, fromLink, draft, partner?, page?, hidden?,
  capped?, pageMissing?, partnerMissing?}` a prompt link's ("Automation" below: `brulion://chat`) —
  `draft` put into the field and **never sent** (the user reads it and presses Send), `partner` who
  answers, and a note above the field (`chatLinkNote`, `chatLinkNoteText`; × closes it, a sent message
  takes it away) that says it came from a link — with the warning tint "This prompt came from a link
  with N hidden characters, taken out now: they can hide instructions. Read it before you send it."
  when it had some —, that it was cut at 8 000 characters, that its page is not in this notebook or
  its partner not set up here (the default partner answers instead: "Who answers a new one" below). The first message of a new one replaces the route with the
  conversation's (`AppController.replaceRoute`; in the side panel, where the view also opens, it
  pins the conversation there again). Title: the conversation's name without its date, else "AI
  chat".
* **Layout**: from 720 px (not in the side panel) the list (300 px) beside the conversation; under
  that one of them — a conversation opened from the list is a step of the history, Back returns.
  The list (`chatConversations`, `ChatListModel`): an item per conversation
  (`chatConversation_<name>`) — its name, its last message as one line (🤖 before the AI's), when
  it last changed, or while its turn runs a small spinner (an hourglass while it waits for the model
  on the device: the rows' `running` / `queued`) — and "New" (`chatNew`); empty: "No conversations
  yet" with the folder named.
* **Its sections** (`section`: ListView sections, `chatSection_<name>` headers): **Pinned** on
  top (the rows' `pinned`: the page's `przypięta: tak` / `pinned: true`, docs/SYNTAX.md §14; a pin
  icon), then the others — "Recent", named only under pinned ones —, newest change first; the
  **archived** (`zarchiwizowana: tak` / `archived: true`) out of it, folded at the bottom as
  "Archived 12" (`chatArchived`, the list's footer, `archivedCount`): a click unfolds them
  (`showArchived`) under their own header, which folds them again. Archived wins over pinned. Side chats
  (below) are never rows — nor matches of the search —, unless the conversation they name is gone.
* **Archived by themselves** (T3 Code's auto-settle): **Settings → Extensions → AI → Archive conversations
  untouched for** (`aiChatAutoArchiveSetting`, `AppSettings.aiChatAutoArchive`, key `ai/chatAutoArchive`:
  **Never** — the default —, **1 day**, **3 days**, **A week**, **30 days**; a device setting) — when the
  notebook is ready and then every hour (`WorkspaceChats.setAutoArchiveDays()` / `archiveUntouched()`, the
  workspace's clock) every conversation whose last message (its `czas::`, else its page's last change) is
  older is archived as the menu archives it (`setArchived`, its frontmatter): not one that is pinned,
  archived, a side chat, running or waiting for the model, asking, showing a failure, unread, with messages
  waiting or a draft, on screen in a chat view, or that a sub-conversation will wake. A toast says "Archived 3
  conversations untouched for a week" with **Undo** (all of them back: `AppController`'s
  `m_autoArchiveToasts`, `WorkspaceChats::autoArchived`). Never in the documentation notebook.
* **What needs the user** (T3 Code's "came back to you"): a row whose turn asks something
  (`asking`: a change to allow, an agent's permission or sign-in — `chatAsking`, a question mark in
  the accent, the row's line what it asks, `question`), whose turn failed (`failure`: `chatFailed`,
  the alert icon and the reason as its line in the danger tint) or with an **unread** answer
  (`unread`: an answer written while no chat view showed it — `chatUnread`, an accent dot, its
  name semibold, its time in the accent; a view that shows the conversation reads it) comes to the
  top of its section, the newest that came back first (`WorkspaceChats.cameBack`); a turn that
  runs leaves the row where it is. Unread is kept in the notebook's device state (LocalStore
  `chat/unread`: {path in lower case → when}), never in the page, so it syncs no change; it follows
  a rename and goes with the page.
* **Search** (`chatSearch`, a `BSearchField` above the list, shown when there are conversations):
  `ChatListModel.query` keeps the conversations whose name or one of whose messages has all of
  its words (case and diacritics aside: folded, `searchTerms`) — the archived ones too, under their
  header (unfolded, not foldable meanwhile). A name's match marks its words in the title
  (`titleMatches`); a message's, the newest such one, shows its line with the most of the words
  as the row's line (`matchText`: plain words, from a little before the first one when it is far
  in — `ChatListModel::matchExcerpt` —, the words marked: `matchMatches`, `TextHelper.markMatches`)
  and opens the conversation at that message (the route's `line`, `matchLine`: the message is
  scrolled to the middle and its bubble has the accent's border for 2.5 s). The model keeps the
  messages' text (and its folded form) it read at its last refresh, so a keystroke reads no page;
  nothing found: "No conversation found" (`chatNoMatch`).
* **Find in the conversation** (T3 Code's find in thread): where the list is not shown — a thread of
  Code mode, a conversation on a phone — Ctrl+F (`page.find`) opens a find bar over the messages
  (`chatFind`: `chatFindField`, `chatFindCount` "2 of 5", `chatFindOlder` / `chatFindNewer`,
  `chatFindClose`): the messages holding every word typed, case and Polish marks aside
  (`ChatMessageModel.rowsMatching`), from the newest; Enter goes to an older match, Shift+Enter to
  a newer one, the match's message has the accent's border, Escape closes it.
* **Keyboard** (desktop): the find command's key (Ctrl+F, `page.find`) while the chat has the focus
  goes to the search field, ↓ there to the first row; ↑ / ↓ move in the list without opening
  (`keyNavigationEnabled`), Enter opens the row (a match at its message), **Alt+1 … Alt+9** open the
  list's first nine conversations (reserved in ShortcutMap, area "chat": no command takes them), F2
  renames the row with the keyboard (else the conversation shown).
* **Notifications** (`WorkspaceChats::notificationWanted` → `workspaceintegration.cpp`): a turn
  answered (the answer's first line), failed (why) or asking (its question's first line) shows a
  system notification titled with the conversation's name — the platform's notifier as a message
  of the app (`notify::isMessage()`, its id `brulion-message:<path>`: one per conversation, a newer
  one replaces it; the desktop's has **Open** and OK, no Snooze; Android shows it as a timer's
  end, "OK" alone; the browser's while the page is open); a click opens the conversation
  (`AppController::openNotification`: a conversation goes to the chat, not the editor). **Settings →
  Extensions → AI → Notify when the AI answers** (`aiChatNotifySetting`, `AppSettings.aiChatNotify`,
  key `ai/chatNotify`): **Always** (also while the app is in front, when no chat view shows the
  conversation), **In the background** (only while the app is not active, the default) or
  **Never**; never for a conversation shown in an active window. The app lock's "Hide the note's
  text in notifications" makes it "AI chat" — "Something new in a conversation". The toast for a
  question nobody sees stays as it was.
* **An AI agent's own notifications** (T3 Code's notifications from the agent; `chatNotify`, the tool
  `brulion_app_chat_notify`, docs/CLI.md "Conversations"): an agent tells the user what they asked to
  hear of ("let me know when the build is done") — `ChatControl`'s `chatNotify` →
  `NotificationCenter::post` ("Notifications" above): a system notification while the app is not in
  front, else a toast with **Open**, kept in the list of notifications. Its title always says who
  sends it ("Claude Code: Build", the conversation's partner; a script's its client name): a note an
  agent read cannot make it pass for another app's. A click opens the agent's conversation, or the
  page it named (`page`). At most **10 of a conversation an hour**: the next is refused with
  `retryAfterMs`. **Settings → Extensions → AI → Let AI agents send notifications**
  (`aiAgentNotifySetting`, `AppSettings.aiAgentNotify`, key `ai/agentNotify`, on by default): off,
  every one is refused (`reason: off`) and the agent says it in its answer instead.
* **Who answers a new one**: **Settings → Extensions → AI → New conversations answered by**
  (`aiDefaultPartnerRow`, its button `aiDefaultPartnerSetting` and menu `aiDefaultPartnerMenu` of the
  chat's partners; shown with more than one partner or a choice made; `AppSettings.aiDefaultPartner`,
  key `ai/defaultPartner`, this device's — agents are set up per device): the built-in assistant (the
  default), an AI agent or an extension's chat command. `WorkspaceChats::defaultPartner` is that
  choice while it answers here, else "" (the assistant; the row then says the one chosen is not set up
  here) — `defaultPartnerChanged` also when the partners change. A new conversation's chip starts with
  it (`ChatView.newPartner`, set again when the default changes and nobody picked another), and so do
  prompt links that name nobody or one not set up here (`carryOutAutomation`), "Add…" of the scheduled
  conversations, and `chatStart` / `chatScheduleAdd` without a `partner` (`""` names the assistant).
* **The list's menu** (`chatRowMenu`: a right-click, a long press): **Pin** / **Unpin**
  (`chatPin`, not on an archived one) and **Archive** / **Unarchive** (`chatArchive`):
  `WorkspaceChats.setPinned()` / `setArchived()` write the page's frontmatter (`chat::setMark`, the
  notebook's language), a toast says so with **Undo** (`AppController.toastUndoChatMark`, also once
  the view is gone); **Rename…** (`chatRename` →
  `chatRenameDialog` with the name without its date, `chatRenameField`; `WorkspaceChats.rename()`
  keeps the date in front and goes through `Workspace::renamePage`, so the day's link follows; the
  toast's **Undo** renames it back — `AppController.toastUndoRename`, also once the view is gone),
  **Open as a page** and **Move to trash** (`chatDelete`: its turn stops, `WorkspaceChats.remove()`,
  the toast's **Undo** restores it — `AppController.toastRestorePage`; the conversation shown goes
  back to the list).
* **A conversation** (`chatMessages`, `ChatMessageModel`): the user's messages right-aligned in the
  accent's tint, the AI's left-aligned on a card (at most 82 % / 92 % of the width, 720 px), each a
  `BRichText` (`measured`: the bubble as wide as its text, a tinted one too — its implicit width
  otherwise 0, which left a bubble a few letters wide) of the message rendered by `InlineRenderer`
  (`ChatMessageModel::renderMessage`: a
  paragraph per block, lists indented with their bullets or numbers, a task's box, headings bold,
  tables as rich-text tables — the header bold, the columns aligned as the delimiter row says, each
  cell's text rendered and escaped, the columns laid out in the bubble's width —, code
  preformatted — never a note's own HTML), its pictures above its text (`chatPicture`:
  thumbnails up to 280 px wide, a tap opens the picture), the time it was written under it
  (`chatTime`, "09:12": its `czas::` line; none on a page written before times were kept), and above
  the first message of a day that day (`chatDay`: "Today", "Yesterday", "15 Nov"); a click on a link
  follows it (page:, tag:, ext: through `PageTools.openLink`, from the conversation's page), a long
  press or right-click has Copy (the message's Markdown), "Show in the page" (the page at its
  line), on the AI's **Report response…** (below), **Quote** (`chatMessageQuote`: its text as a quote, "> …" lines, at the start of the field —
  `WorkspaceChats.quote()`, core `chat::quoted`), **Edit from here** (`chatMessageEdit`, the user's
  messages: as T3 Code's rewind — the message and every one after it come out of the page
  (`WorkspaceChats.rewind()`, core `chat::truncated`: the page before it byte for byte), its turn
  and its waiting messages stop, its text and pictures go back into the field; an AI agent's next
  turn is a new session with what is left; a toast's **Undo** puts the page back while it did not
  change since — `undoRewind()`, AppController's `m_rewindToasts`; in a thread of Code mode whose turn
  of that message has a checkpoint, a dialog first, `chatEditFromHereDialog`, with T3 Code's "Revert
  files too": "Also put the project's files back as they were before this message" —
  `WorkspaceChats.checkpointOf(path, number)` / `restoreCheckpoint(path, id)` →
  `checkpointRestored(path, ok, message)`, a toast) and **Fork from here**
  (`chatMessageFork`: a new conversation, today's, a copy of this one up to that message —
  `WorkspaceChats.fork()`, core `chat::forked`, its journal line too — that goes on by itself with
  the same partner and a session of its own; its frontmatter's `fork:` names the source,
  docs/SYNTAX.md §14, and its header has a chip "Forked from …", `chatForkSource`, that opens it),
  and on the user's own messages **Copy prompt link** (`chatCopyPromptLink`: a `brulion://chat`
  link with the message and the conversation's partner, no notebook named —
  `AppController.chatPromptLink()`; the palette's **AI: Copy prompt link**, `ai.copyPromptLink` while
  the chat shows, copies the draft's with its page through the uiAction `chat:copyPromptLink`) — on a drawing (below) also **Copy source** (`chatMessageCopySource`).
  Above them a row with who answers (`chatPartner`, shown when an extension offers a partner
  or the page names one: its menu, `chatPartnerMenu`, changes it for the next turns —
  `WorkspaceChats.setPartner()` writes the page's `partner:`), the page it is about (a chip that
  opens it), "Open the conversation as a page" (`chatOpenPage`) and, without the list, "New
  conversation". New messages scroll the list to its end (`WorkspaceChats.written`, the model's
  count).
* **Side chats** (T3 Code's side chat, `/btw`): a question asked beside the conversation without adding it to
  it — `/btw <question>` sent from the field (`WorkspaceChats.isAside()` / `asideQuestion()`: "/btw" alone or
  before a space or a line break, any letter case; the app's own command, never sent to an agent, also while
  the conversation's turn runs and with Ctrl+Enter; offered first among the slash-command chips, `chatCommand`
  "/btw", in any conversation that is no side chat, whoever answers — `agentCommands()` gives it with `app`
  true and leaves an agent's own `btw` out), or the header's **Ask a side question** (`chatAskAside`: "/btw " at
  the start of the field). `WorkspaceChats.askAside(path, question, pictures)` writes the side chat — a page of
  its own in `AI/` named after the question, a copy of the conversation with the question after it
  (`chat::sideChat`, its `side:` and `side-from:`, docs/SYNTAX.md §14; no journal line) — and runs its turn: the
  conversation's partner, an agent's session of its own, the model on the device in line as any turn; the
  conversation's history never has it. It opens as a step of the history (`{view: "chat", path, side}`: Back
  closes it) in **`chatSidePane`** (`qml/views/ChatSidePane.qml`) — beside the messages when the conversation
  has 860 px or more (a pane of 340–440 px), else in their place: its name (`chatSideTitle`), **Bring into
  the conversation** (`chatSideBringIn`: its last answer, `lastAnswer()`, as a quote at the start of the
  conversation's field — `chat::quoted`; a narrow pane closes for it), **Delete** (`chatSideDelete`: to the
  trash, the toast's Undo restores it) and **Close** (`chatSideClose`; Escape in the pane); its own messages
  only (`chatSideMessages`, `chatSideUser` / `chatSideAi`: `ChatMessageModel.ownOnly`, from its `side-from:`
  on; a long press or right-click: Copy, Bring into the conversation), the answer as it is written
  (`chatSideLive`, `chatSidePartial`), what the AI does or that it waits for the model on the device
  (`chatSideWorking`), a question of its turn with its options (`chatSideQuestion`,
  `chatSideQuestionOption`; the pane tells `showing()`, so the question is answered there), a failure with
  **Try again** (`chatSideFailure`, `chatSideRetry`), its waiting messages (`chatSideWaiting`), its field
  (`chatSideInput`: Enter sends, in line after the answer while it runs) and Send / Stop (`chatSideSend`,
  `chatSideStop`). Under the header the conversation's side chats are chips (`chatSideChats`,
  `chatSideChat`: its name, its state's icon; the open one selected; `lineage.sideChats(path, serial)`). A
  side chat is never in the list, its search, its sections, never unread and never a notification
  (`WorkspaceChats` leaves it out: `sideOf()`); for agents and scripts it is no row of `chats` and no
  conversation of a name (its path reads it, its row has `side`). A rename of the conversation rewrites its
  `side:` link; `remove()` of the conversation moves its side chats to the trash with it and its toast's Undo
  restores them all (`lastRemoved()` → `AppController.toastRestore`). A side chat has no side chat of its own.
* **Reporting an answer** (Google Play's AI-Generated Content policy asks for it; every build has
  it): **Report response…** (`chatMessageReport`) opens `aiReportDialog`
  (`qml/shell/ShellAiReportDialog.qml`; `AiReportDraft`, `shell/aireportdraft.h`, over core
  `aireport/aireport.h`) — a reason, one of six chips (`aiReportReasons`, `aiReportReason_<id>`:
  offensive or hateful, sexual content, violent or dangerous, harassment, false or misleading, other;
  needed for Send), a comment (`aiReportComment`, optional) and **what the report holds**: the answer
  in an editable field (`aiReportResponse`: the user may shorten it or take a private part out), the
  user's question before it only with its box ticked (`aiReportQuestionBox`, off — "your question
  and the rest of the conversation stay out of the report"; then `aiReportQuestion`, editable:
  `ChatMessageModel.questionBefore()`, the user's messages of that turn without their pictures —
  never the rest of the conversation or the page it is about), who answered (`aiReportFacts`:
  `WorkspaceChats.modelName()` — "Brulion's AI assistant · Anthropic · claude-opus-5-5" from the
  settings now, never a server's address; an extension's partner by its id; an agent with its model
  option) and the app's version and device (`platform::deviceDescription()`: no user or host names;
  "(Google Play)" in that build). **Show the report's text** (`aiReportPreviewToggle`,
  `aiReportPreview`) shows its title and text exactly as they go, as plain text. The report is in
  English (it goes to the developers), the comment, the answer and the question each in a code
  fence longer than any run of backticks in it, so Markdown, HTML or a mention in them stays text on
  GitHub. **Send report** opens it through `Workspace.openUrl` (the app's one way to the browser and
  the mail app: http, https and mailto only): with the CMake cache variable
  `BRULION_AI_REPORT_EMAIL` (an email address, checked when configuring; `scripts/android-build.sh`
  passes it on from the environment) a `mailto:` with subject and body (RFC 6068: CRLF lines), else
  a new issue of github.com/janusz-bit/brulion with `title` and `body` in its address — the dialog
  says which, and that an issue there is public (`aiReportDestination`). Every character but the
  unreserved ones is percent-encoded (a "+" never turns into a space); a link longer than 7000
  characters (`aireport::MaxLinkLength`: GitHub refuses one of some 8 KB) gets its long texts cut to
  one length that fits — the short ones whole, never inside a surrogate pair —, a line under each
  saying how much of it is there, and the dialog says so (`aiReportCut`). Brulion sends nothing
  itself: the user sends the issue or the email there. The answer is then marked **reported**;
  **Hide this response** (`aiReportHide`) marks it **hidden** and sends nothing. A marked answer is
  a line in its place, "Reported response" / "Hidden response" with **Show** (`chatFlagged`,
  `chatFlaggedText`, `chatFlaggedToggle` — then **Fold**: `ChatMessageModel.reveal()`, the row's
  `revealed`, while the view shows the conversation); the list's preview of a conversation whose
  last message it is says the same, and its menu has **Remove the “Reported” mark** / **Unhide
  response** (`chatMessageUnflag`). The mark is kept in the conversation's page
  (`WorkspaceChats.setFlag(path, number, "reported" | "hidden" | "")` → `chat::setFlag`: the
  frontmatter's `zgłoszone:` / `ukryte:`, docs/SYNTAX.md §14), so it syncs and stays after a
  restart; another app and an older Brulion show the answer as before. The model still gets a
  marked answer with the conversation (the mark is the user's view of it). The writing assistant's
  result has the action too ("AI in the editor").
* **Drawings in an answer** (T3 Code's visual replies, Brulion's way: no web view, nothing of the
  message runs): an AI message with a closed ```` ```chart ```` block whose table or calc block is in
  the message itself, a closed ```` ```mermaid ```` diagram of a type Brulion draws, a
  ```` ```math ```` / `latex` / `tex` block or a paragraph that is one `$$…$$` formula is shown in
  parts one under another (`chatSegments`, `ChatSegments.qml` → `ChatSegment.qml`; the model's
  `segments` role, `ChatMessageModel::renderSegments()`: [{kind `text` | `chart` | `diagram` |
  `math`, html, code, source, chartData}]): the text between them as the bubble's rich text, a chart
  by the editor's `ChartView` (`chatSegmentChart`: the numbers through `chart::resolveData` of the
  message's own tables, numbers in the chat's language; a message runs no query), a diagram by
  `MermaidView` (`chatSegmentDiagram`: scaled down to the bubble, never up), a formula as the
  editor's `renderMath` rich text, centred and larger (`chatSegmentMath`). The bubble keeps its
  width rules — as wide as its widest part (`naturalWidth`; a chart takes all of the 82 % / 92 %,
  720 px). A message without a drawing stays one rich text (`messageHtml`, as before), and a drawing
  that cannot be drawn shows its block as written: a chart's mistake or a column not there, a query,
  an unterminated fence (an answer cut off), a Mermaid type not drawn; a diagram whose source fails
  only when laid out falls back so in the view (MermaidView's `status`). A tap on a drawing opens it
  large as a picture opens (`chatDrawingViewer`, `ChatDrawingViewer.qml`: a chart at the window's
  width up to 1400 px, a diagram fitted — a small one laid out again with a larger font, up to
  twice the text's, a large one scaled down; a click or `chatDrawingViewerSize` shows it at its own
  size, scrolled —, a formula large; "Copy source", `chatDrawingViewerCopy`, the block as written;
  Close or Escape). The answer being written draws them too once their block is finished
  (`chatPartialParts`: `ChatMessageModel.segments()` of `ChatTurn.partialSettled`, rendered again
  only when a block is finished — never per token; the block being written stays text). AI agents
  are told they may answer so (`WorkspaceAgents::context()`); the built-in assistant's prompts are
  not changed (a model on the device trained on them: docs/FINETUNE.md). The HTML exports of the
  conversation's page draw them as any page's.
* **A conversation too long for the model** (`WorkspaceChats.historyBudget`: 16 000 characters for
  the model on the device, 120 000 for a provider on the network — `chat::firstSent`, a picture
  counting 3 000): the turn sends it from a later turn on, whole turns from the oldest left out, the
  latest always sent; the partner gets `ctx.omitted` (the built-in assistant tells the model). The
  view shows it: the messages the model does not get are faded, and a line above the first one it
  gets says "The AI does not get the N messages above: too long" (`chatNotSent`; "the model on this
  device …" with that one).
* **While the AI answers** (the conversation's `ChatTurn`, `Workspace.chats.turn(path)`, runs): the
  answer as the model writes it, as PocketPal shows it — a card like the AI's messages (`chatLive`):
  first what the model thinks, **collapsed** to one line of its end in small grey italics
  (`chatThoughtsToggle`, `chatThoughts`, `ChatTurn.thoughts`; "Thinking…" while no words came,
  "Thoughts" after) — a tap opens all of them (`chatThoughtsAll`, `ChatTurn.allThoughts`): plain
  text, the model's own words (a model that shows its thinking — the one on the device, a server's
  `reasoning_content`, Claude's summarised thinking: docs/EXTENSIONS.md "AI") —, then its words
  rendered as its message will be (`chatPartial`: `ChatMessageModel.render()` of `ChatTurn.partial`),
  at most ten times a second, the list following them while it is at its end (scrolled up, it
  stays). The words are two texts: the answer's finished blocks (`chatPartialSettled`,
  `partialSettled`: rendered again only when a block is finished — Qt's "Calculating text layouts can
  be a slow operation") and the block being written (`chatPartialTail`, `partialTail`,
  `WorkspaceChats::splitWritten()`: cut before the last block at the margin after a blank line, never
  inside a code block), which with the thoughts has `mutabilityGroup: Item.DynamicMutabilityGroup`
  (Qt 6.12; `BRichText.mutability`): the renderer keeps it out of the other texts' batches, which are
  not uploaded again with it. Under them, with the model on the device, its speed (`chatSpeed`).
  Each request of the assistant's loop starts the card anew: after a tool call the next round's
  thoughts. The written answer's message takes the card's place in one frame
  (`WorkspaceChats.written` comes before the turn ends; the view refreshes its model at once).
  Under it, while no words come or a tool runs, a spinner and what the AI does (`chatWorking`,
  `chatStatus`: `ChatTurn.status` from `b.status` — "Czytam „Dom”…" —, else "The AI is
  answering…"); while the words come there is no spinner: its frames would take a fast core from a
  model on the device (docs/PERFORMANCE.md "The model on the device"). The send button becomes
  **Stop** (`chatStop`: `Workspace.chats.stop(path)`: nothing is written for that turn, the question
  stays for Try again, and the request itself ends — the model on the device stops writing, a
  provider's connection is closed —, so it holds up no other question).
* **Writing on while it answers** (as T3 Code's queue): the field stays open; Enter (or the send
  button, a clock then: `chatQueue`) puts the message in line — it waits under the answer as a
  bubble with the accent's outline (`chatWaiting`) with **Send now** (`chatWaitingNow`), **Edit**
  (`chatWaitingEdit`: back into the field, its pictures too) and **Remove** (`chatWaitingRemove`),
  and goes by itself, a turn of its own, when the answer is written (`WorkspaceChats.queue()`,
  `queued()`, `unqueue()`, `sendNow()`). **Ctrl+Enter** sends it now (`steer()`: the turn stops — an
  agent takes no message in the middle of one — and it is answered first, with the question the
  stopped turn did not answer). With an empty field the button is Stop. A turn that fails or is
  stopped **holds** them (`chatHeld`: "2 messages wait: the answer before them stopped." with
  **Resume**, `chatResume` → `resumeQueue()`; a new message resumes them too, and waits after
  them; held sub-conversations' wake messages alone: "1 message from sub-conversations waits for you.",
  `heldForWake()`); they are kept in the notebook's device state
  (LocalStore `chat/queued`), so a restart keeps them, held, and they follow a rename.
* **Naming a page** (as T3 Code's @-mentions): "@" and the start of a name (at the start or after a
  space — not an address), or "[[" and anything but "]", offers the pages it may be as chips above
  the field (`chatMentions`, `chatMention`: `WorkspaceChats.mentions()` through
  `Workspace.suggestPages`, at most 8); a tap or Tab (the first) writes its link, `[[Zakupy]] `
  (`withMention()`, `mentionEnd()`). An AI agent gets the pages its newest message names — at most
  5, not the conversation nor the page it is about — with the message: their text where it takes
  embedded context, else links (an encrypted page: a link); the built-in assistant reads them with
  its own tools.
* **Drafts**: what is written in the field and not sent — its text and its pictures — stays with
  its conversation while another is shown, and over a restart (`WorkspaceChats.draft()` /
  `draftPictures()` / `setDraft()`, the notebook's device state `chat/drafts`: an entry is the text,
  or `{text, pictures}` with pictures — an older version's text-only entry is read as it is —,
  following a rename; a new conversation's draft under "", a new thread of Code mode's under
  `code:<project>`). The view keeps it when another conversation is shown, when it closes, before the
  camera app opens and when the app leaves the front (`Workspace.saveOpenEditorsRequested`: Android
  may end the app in the background without a word). Pictures a draft keeps stay in the notebook
  when it closes (those added and neither sent nor kept go then).
* **Questions asked before**: Up in an empty field brings back the user's questions of the
  conversation, newest first (Down goes forward again, to an empty field; editing one makes it a
  new message — `WorkspaceChats.prompts()`, core `chat::prompts`: without their pictures and the
  first one's link to the page it is about).
* **Several conversations at once**: every conversation has a turn of its own — one per
  conversation (a message written meanwhile waits for it: above) —, and with a
  provider on the network they all answer side by side. **The model on the device** answers one turn
  at a time: the others wait in line, in the order they were sent (`ChatTurn.queued`, its `place`):
  the conversation shows an hourglass and "Waiting for the model on this device: it answers another
  conversation first." (or how many are before it), Stop takes it out of the line. A turn goes on
  when the view closes: its answer is written into the page anyway (`WorkspaceChats`); the top bar's
  ✨ spins while one runs that no chat view shows (`WorkspaceChats.runningShown`).
* **A change to allow**: the question of this conversation's turn as a card (`chatQuestion`, the
  accent's border): the first line in bold, the lines after it as plain monospace text on a well
  (`chatQuestionDetails`: they quote notes), its options as buttons (`chatQuestionOption`: Allow
  primary, the second secondary, No ghost; `Workspace.extensions.answerChat(path, option)`). Two
  turns that ask at once never cancel each other: the second waits behind the one shown
  (`WorkspaceExtensions::chatQuestion*()`: each conversation shows its own, shown or waiting), and a
  command's question that comes meanwhile is shown first. The view tells
  `Workspace.chats.showing(path, true)` while it shows the conversation, so `ShellExtensionQuestion`
  leaves the question to it — and asks it in its dialog once no view shows it (another view, the
  panel closed); answered in one, the other goes.
* **A secret the agent asks for** (T3 Code's `request_secret`; `chatSecret`, `ChatSecretCard.qml`;
  `Workspace.chats.secrets`, `shell/chatsecrets.h`, core `secretdrop`): an AI agent of this computer
  that needs a token, a password, a PIN or a key only the user can give calls
  `brulion_app_chat_request_secret` (docs/CLI.md "Conversations": the remote control's
  `chatRequestSecret`) for its own conversation while its turn runs — never asks for it in the chat,
  where it would be kept in the conversation's page (synced, in its history) and sent to the model;
  the context and the MCP server's instructions say so. The conversation shows a private card under
  the answer (the accent's border): "Claude Code asks for a secret: GitHub token" (`chatSecretTitle`),
  the reason (`chatSecretReason`) — both the agent's words as plain text, never HTML —, a sign-in or
  setup page's button when the agent gave one (`chatSecretOpen`, "Open github.com": http and https
  only, through `Workspace.openUrl`), a masked field (`chatSecretField`, the agent's placeholder,
  `secretText`: the remote control never reads it, also while **Show** shows it — `chatSecretShow`),
  **Paste** (`chatSecretPaste`), **Send** (`chatSecretSend`, Enter) and **Cancel**
  (`chatSecretCancel`), and a line on where it goes (`chatSecretNote`). Sent, the value goes to the
  agent as the path of a file only this user can read (0600, in a 0700 folder of this run under the
  user's runtime folder, `$XDG_RUNTIME_DIR/brulion-<id>-secrets/run-…/`, never on a disk where there
  is one; else the app's cache), with a note to read it only inside the command that uses it
  (`--token "$(cat '<file>')"`); the file is deleted when the conversation's turn ends (it is
  answered, fails or is stopped; the agent's process ending ends its turns), after 15 minutes, or
  when the app ends — and what a crashed run left at the next start (its folder's lock held by no
  live process). An agent that cannot use a file asks for `delivery: value`: the value itself then
  goes into its context and its provider's logs, which the card says in the warning tint. Either way
  the value is registered with the event log's redaction while it lives and is never written into
  the page, the event log, a notification, a toast or the notebook's device state; the field is
  emptied when the card goes. While a card waits the conversation **asks** (its row, `stateOf` —
  an agent that delegated to it learns `waiting_for_user` —, the notification) and, when no chat view
  shows it, a toast "Claude Code asks for a secret: GitHub token" with **Open**. One card per
  conversation at a time (a second request: `busy`); Cancel (`refused`, `cancelled`), Stop of the
  turn (`stopped`), the turn's end, the agent's call gone, its time (5 minutes by default, at most 15:
  `timeout`) or the notebook closed end it. A script of the user's (the remote control on) names the
  conversation; its card says "An AI agent". Tests: `tst_secretdrop`, `tst_shell_agents` (`secrets`;
  `realAgentAsksForASecret` with a real agent: Claude Code (Haiku) asked through the real
  brulion-cli, counted the value's characters in a command reading the file, and the value was in
  no page — 2026-11-28).
* **A failed turn** (`chatFailure`, `ChatTurn.failure`): the reason in the danger tint ("AI: no
  connection: …", "The AI assistant is off: …", "The chat partner “…” is not running: …"), **Try
  again** (`chatRetry`: the turn again on the page as it is) and ×.
* **A new conversation**: an empty state ("Ask about your notes") with suggestions as chips —
  "Summarise this page" (with a page), "What do I have to do this week?", "What did I write
  yesterday?", "Plan my tomorrow" — sent when tapped; in the messages' place (`chatEmptyFlick`),
  scrolled when the keyboard leaves too little room, so the field always stays above the keyboard
  (it once pushed the field under it); above the field the page it is about as a
  removable chip (`chatPage`, "goes along with the question") and, when an extension offers a chat
  partner, who answers (`chatNewPartner`, "Answers: AI assistant ▾": the same menu).
* **Pictures with a message**: the paperclip (`chatAttach`) picks pictures from files (`FilePicker`:
  PNG, JPEG, WebP, GIF, BMP — several at once; in the browser its own file picker,
  `WorkspaceChats.pickPicturesInBrowser()` over `platform/wasm/wasmfiles.h` — `browserPicker` —: each
  picture at most 50 MB, a pick 200 MB, read into memory and stored as a file's are, the pick's
  pictures handed to the view that asked, `picturesPicked(pick, paths)`) or, on Android, opens a
  menu (`chatAttachMenu`) with
  **Take a photo** (`chatTakePhoto`: the system's camera app, `platform/capture.h`'s `PhotoCamera`)
  and **Picture from a file…**; the photo goes into the draft of the conversation it was taken for —
  `WorkspaceChats.takePhoto(draft)`, then `placePhoto()`, the one place that decides: copied into
  the notebook as a picked file is, added to that draft's pictures, `photoTaken(draft, path)` for a
  field that shows it —, also when Android ended the app while the camera app was in front: the
  camera keeps the photo with its notebook and draft (docs/ANDROID.md "Camera and dictation"), and
  the next start puts it into that draft once the notebook is open; Ctrl+V with a picture on the clipboard (not with text) adds it, and pictures dropped
  on the conversation do too (`DropArea`, `chatDropHint` while they hover). Each is stored at once as
  an attachment of the notebook (`WorkspaceChats.addPicture` → `Workspace::importAttachment`, the
  attachments folder) and shown as a thumbnail above the field (`chatAttached`,
  `chatAttachedPicture`) with × (`chatDetachPicture`: deleted again — `dropPicture`; pictures not
  sent stay with the conversation's draft, above); sent, they are embeds in the user's message (`![[…]]`,
  docs/SYNTAX.md §14), and the model gets them as pictures (b.ai's `images`; the newest eight of the
  conversation). A model on the device that reads text only gets their names, and the field says so
  (`WorkspaceChats.seesPictures`).
* **Dictation** (`chatDictate`, the microphone, shown while `WorkspaceChats.dictationAvailable`):
  Android's speech recognizer through its intent (the system's dialog listens; no microphone
  permission of Brulion's own); what it heard goes into the field at the caret. On the desktop
  (Linux, Windows) the app's own: `media::Dictation` (`src/app/media/dictation.h`), registered by
  `AppController::setUpSpeech()` with `platform::setSpeechRecognizer()` where this build transcribes
  (`Transcriber.available`) and records — a `VoiceRecorder` (the system's default microphone, WAV into
  the cache's `dictation` folder) heard on the device by the voice notes' Whisper model
  (`Transcriber::transcribeFile()` → `heard()`: nothing placed in a note; in the dictation's language,
  the UI's, unless Settings → Speech names one). Pressed, it records: the button becomes a stop square
  in the danger colour, the field's frame red and read-only ("Listening…"), and beside the button the
  time recorded of its five minutes (`chatDictationTime`, "0:12 / 5:00", `WorkspaceChats.dictationElapsed`
  / `dictationLimit`); pressed again, **Enter** or Send finish it ("Transcribing…",
  `WorkspaceChats.recognizing`), and the text goes in at the caret; **Esc** (in the field, or a
  `Shortcut` while the focus is elsewhere) throws it away. At five minutes it stops by itself and is
  heard. While it records, with **Text while recording** on (Settings → Speech), the words heard
  so far stand in the field at the caret (`Dictation` emits `partial()` from its `LiveTranscript`,
  `WorkspaceChats.dictationPartial`; ChatView keeps their range, `partialAt` / `partialLength`, and
  replaces it with each new text), and the whole recording's text takes their place when it is
  heard — or nothing, when it is cancelled or fails. The recording is deleted when heard or
  cancelled, and leftovers of an app that ended meanwhile when the next dictation starts. Without a
  speech model (`WorkspaceChats.dictationReady`
  false) the microphone opens `chatDictationSetup`: what it needs (the model's name and size), its
  download with the progress (`chatDictationDownload`, the voice notes' `AppController.speech`), and
  **Speech settings** (`chatDictationSettings`); once the model is there "Dictation is ready" with
  **Dictate**. A recognizer tells `SpeechRecognizer::ready()`, `elapsedMs()`, `maxMs()`,
  `recognizing()`, `partial()` (`platform/capture.h`; the system's: ready, no time, no partial text —
  its dialog shows its own). Tests: `tst_media_dictation`.
* **The field** (`chatInput`, a `BTextArea` in a frame that grows to 168 px, then scrolls with the
  caret kept in view): with a keyboard Enter sends and Shift+Enter is a new line; on touch the
  keyboard's Enter is a new line and the round button sends (`chatSend`, an arrow up, disabled
  without text or pictures). The view keeps above the on-screen keyboard as every view does
  (AppShell's `keyboardRoom`); an existing conversation does not take the keyboard on touch when
  opened.

`WorkspaceChats` (`src/app/shell/workspacechats.h`, QML `Workspace.chats`): `send(path, text, page,
pictures, partner)` writes the message with its time and pictures (a new conversation: its page,
then the journal's line) and runs the turn (`WorkspaceExtensions::runChat` → the partner's chat
command with `chat::history()` of the page from `chat::firstSent()` on, its page and `ctx.omitted`);
`chatAnswered` appends the AI's message with its time where the page is now (a rename followed; a
page deleted meanwhile ends the turn), `chatFailed` keeps the reason in the conversation's
`ChatTurn`; `turn(path)` (a `ChatTurn`: `running`, `queued`, `place`, `status`, `failure`,
`omitted`, `partial`, `partialSettled`, `partialTail`, `thoughts`, `allThoughts`, `speed`),
`retry(path)`, `stop(path)`, `stopAll()`, `dismissFailure(path)`, `isConversation(path)`,
`showing()` / `isShown()`, `rename()`, `remove()`, `setPinned()` / `setArchived()`, what needs the
user (`isUnread()`, `cameBack()`, `failureOf()`, `attentionChanged`) and its notifications
(`setNotifyMode()`, `notificationWanted`), `nameOf()`, `partnerOf()` / `setPartner()`,
`setFlag()` (an AI's answer reported or hidden) / `modelName()` (who answered, for a report), side chats
(`askAside()`, `isAside()`, `asideQuestion()`, `sideOf()`, `lastAnswer()`, `lastRemoved()`), the wakes of
sub-conversations (`setWake()`, `wakesParent()`, `heldForWake()`, `MaxWakesPerHour`), the conversations
archived by themselves (`setAutoArchiveDays()`, `archiveUntouched()`, `autoArchived`), the secrets agents ask for
(`secrets`: ChatSecrets, "A secret the agent asks for" above), the webhooks' server (`hooks`:
HookServer, "Scheduled conversations" below), the
pictures (`addPicture`, `addPictures(To)`, `addClipboardPicture`, `dropPicture`, `pictureUrl`,
`picturePath`), the camera and dictation (`takePhoto(draft)` → `photoTaken(draft, path)` / `captureFailed(draft, why)`, `startDictation` /
`stopDictation` / `cancelDictation` → `dictated`; `dictationReady`, `recognizing`, `dictationElapsed`,
`dictationLimit`, `useSpeechRecognizer()`), the agents' subscription limits (`limits`: AgentLimits,
"AI agents (ACP)" → "Its limits"), `historyBudget`, `seesPictures`, `onDevice`,
`runningCount`, `runningShown`; a turn's partial answer and thoughts (`ChatTurn.partialChanged`,
coalesced to ten a second; `thoughtsTail()`: the last 240 characters from a word on) from
`WorkspaceExtensions::chatPartial` — every request of a chat's turn goes out with an
`ai::PartialCallback` (`ai::Client::send`: a provider on the network streams its answer, the model
on the device hands on its tokens) and is stopped (`ai::Pending::stop`) with the turn, or when the
extensions load again. The model on the device's line is kept here (`m_waiting`, one turn on it at a
time); a change of the provider (`WorkspaceExtensions::aiSettingsChanged`) starts the waiting ones.
Who may answer (`partners()`, `partnerLabel()`, `partnersChanged`: the extensions' and the agents')
and what a turn asks (`asking(path, questionSerial)`, `questionText()`, `questionOptions()`,
`answer()`: a change the assistant asks a yes for, an agent's permission or sign-in) are the
chat's, whoever answers.

### AI agents (ACP)

An AI agent the user runs on this computer answers a conversation like a partner: Claude Code
(through `@agentclientprotocol/claude-agent-acp`), Codex (`@agentclientprotocol/codex-acp`), Gemini
CLI (`--acp`), OpenCode (`opencode acp`) or any program that speaks the
[Agent Client Protocol](https://agentclientprotocol.com) (version 1: JSON-RPC 2.0, a message a line
on its standard input and output). The agent signs in by itself — the subscription its own program
knows how to use, or a key of its own — so the app holds none of its credentials. A program runs
only in the desktop app (`WorkspaceAgents::available()`): Android and the browser start none. An
agent **on another computer** runs everywhere (`remoteAvailable()`; in the browser the computer
names the app's site with `--allowed-origin`): a computer of the user's runs it with `brulion-cli acp serve` (docs/CLI.md "AI agents for other devices"), this
device paired with it once; the protocol then goes over `acp::RemoteStream` (core `acp/remote.h`)
instead of a program's standard input and output, and everything below works the same — the
agent's files, terminals and Brulion's tools are that computer's. Tried with Claude Code 0.109,
Codex 2.8 and OpenCode 1.19 (the opt-in test `tst_shell_agents realAgent`,
`BRULION_REAL_ACP_AGENT="<command>"`).

* **Settings → Extensions → AI agents** (`acpAgentsSettings`, shown where agents run): a row per
  agent (`acpAgentRow`: its name, its command — the middle of a long one left out — and whether it
  gets Brulion's tools) with **Check** (`acpAgentCheck`: the agent started in the notebook's folder,
  a session made without a question — a toast says "Claude Code works (Claude Agent 0.109.0). Model:
  Sonnet 5.5 · Mode: Auto" or why not, and the chat learns its options), **Edit…** (`acpAgentEdit` →
  `acpAgentDialog`: `acpAgentName`, `acpAgentCommand`, `acpAgentTools`, and `acpAgentApp` — "Let it
  change the app while it runs", off by default and enabled with Brulion's tools: its tools also get
  the live ones, below — for an agent on another computer its tools there, through that computer
  (not in the browser); the row then says "· may change the app") and Remove
  (`acpAgentRemove`); **Add** (`acpAgentAdd` → `acpAgentAddMenu`: the presets Claude Code, Codex,
  Gemini CLI and OpenCode started with `npx -y …`, or "Another agent…", `acpAgentOwn`, which opens the
  dialog), and **An agent on another computer…** (`acpAgentRemote`, the one entry where no
  programs run) — `acpHostDialog`: the computer's address (`acpHostAddress`) and the code of its
  `acp pair` (`acpHostCode`: none needed in the user's own tailnet, "None in your tailnet"); **Pair**
  pairs (`WorkspaceChats::pairAgentHost` → `WorkspaceAgents::pair`: `acp::pairRemote` — without a
  code the computer judges the device by its tailnet identity —, then `acp::fetchRemoteInfo`), the
  Add button busy meanwhile, and a toast says with whom and which agents, or why not (a wrong or
  expired code, the computer not reached). A computer that wants its code after all (403
  `code_needed`: the device is not its user's in their tailnet) opens the dialog again with the
  address and "That computer wants its code: Settings → AI agents → Pair a device there."
  (`WorkspaceAgents::codeNeeded` → `WorkspaceChats::agentHostNeedsCode`, `hostDialog.needsCode`;
  no toast). Each agent of that computer becomes a row (`raspberry-pi-4-claude`:
  the computer's name and the agent's id there) whose line says "On raspberry-pi-4
  (https://agent.example.com) · with Brulion's tools"; pairing again keeps its name and options.
  Edit… changes its name and "Let it change the app while it runs" there (the computer's tools on
  or off are that computer's). A `brulion://pair?url=…&code=…` address ("Automation" below:
  the QR code `acp pair` shows, opened by the phone's camera) opens this section with
  `acpHostDialog` filled in (the route's `pairUrl` and `pairCode`, taken once); the user pairs. On
  Android the section also has **Notifications** (`acpAgentNotifications`, the reminders' permission
  row): the notification of an agent on another computer at work (below) shows only with it. Kept in the device's settings (`AppSettings::acpAgents`, key
  `ai/agents`: rows {id, name, command, tools, app, options, known} and, for an agent on another
  computer, {url, remote, host, root}; at most 20, malformed rows left out) — never in the
  notebook, which comes from anywhere: a page may only name an agent set up here. The token a
  computer gave this device is in the app's secrets (`<app data>/secrets/acp-<id>`, owner-only,
  masked in every log line), never in the settings; removing the agent removes it.
* **Agents of the user's other computers, from the notebook** (`acpOfferRow`, under the agents;
  `AgentShare.offers`): a computer that shares its agents in a tailnet writes its address and their
  names into the notebook's `.brulion/config.json` (`agentHosts`, docs/CLI.md "A phone finds the
  computer"), and every other device lists them — "Claude Code", "On nixos (http://nixos.tail…
  .ts.net:8790)" — with **Add** (`acpOfferAdd` → `WorkspaceChats::addOfferedAgent`: pairs without a
  code, or with the token of another agent of that computer already here, and adds just that
  agent; the code is asked for when the computer wants it, above). Not this computer's own offer,
  not an agent added already, and only addresses of a tailnet (core `acp/hostoffer.h`). Read again
  when config.json changes (a sync) and when the agents change.
* **Sharing them with the phone** (`acpShareSwitch`, desktop only — `AgentShare.available`: programs
  run here and the command line is found — the same version first: brulion-cli of the app's
  package, else the AppImage or the app's own binary as `brulion cli`, else a brulion-cli on PATH;
  `Workspace.chats.share`, `src/app/shell/agentshare.h`): "Share agents with the phone"
  (`AppSettings.acpShare`, key `ai/share`, off by default) runs `brulion-cli acp serve` for the
  notebook that is open — never the documentation — with this device's agents (programs only; their
  list in `<app data>/agent-share/agents.json`) and Code mode's projects of this device (`--projects
  <app data>/agent-share/projects.json`, written again whenever the projects change: the phone's
  threads of them run here, docs/CODE.md "Threads on the phone") while the app runs (`ServerProcess`,
  `src/app/shell/serverprocess.h`: started again after a crash with waits of 2 s … 1 min, a refusal
  left as it is, stopped with its standard input — `--stop-on-eof` — when it is turned off, the
  notebook or the agents change, or the app quits; every line of its log in the event log). The
  switch's line says what it does: "On at http://nixos.tail1ea20e.ts.net:8790 · your devices in the
  tailnet pair by themselves", "On at http://192.168.1.10:8790 · not encrypted on your network",
  "Starting…", "Add an agent of this computer first.", "Open a notebook of yours: the open one is
  shared.", "Stopped: … — trying again soon". Where it is reached: found by itself — Tailscale when
  tailscaled runs here (`--tailscale`, the computer's MagicDNS name), else this computer's address in
  its network (`--allow-http`) — or **Address** (`acpShareAddress`, `AppSettings.acpShareAddress`,
  its placeholder the address found): an address of this computer, a tailnet name (with https:
  behind the user's own Tailscale Serve), an https:// address of their own proxy. **Pair my devices
  without a code** (`acpShareTrust`, `AppSettings.acpShareTrust`, on; shown in a tailnet): the
  owner's devices there pair by themselves (docs/CLI.md "In a tailnet"). **Pair a device**
  (`acpSharePairRow`: how many are paired, `AgentShare.devices` from `acp clients`, counted again
  when one pairs) — **Show code** (`acpSharePair` → `AgentShare.newPairing()`: `acp pair`) opens
  `acpShareCodeDialog`: the brulion://pair link as a QR code (`acpShareQr`, `QrCodeView`), "Code
  K7QM-2XRP · valid until 10:42, once" (`acpShareCode`) and the address; **Unpair all**
  (`acpShareUnpair`: `acp revoke --all`). In a tailnet it writes its offer into the notebook's
  config.json (above; never over a config.json that is no JSON object) and removes it when turned
  off. Each process of the server gets the app's grant (`RemoteControl::shareGrant()`, the remote
  control's agent sharing: `AgentShare::setAppGrants()` from `main.cpp`, `ServerProcess`'s
  `setLaunchEnvironment()` — a new one each start, also after a crash, revoked when it ends; the
  socket listens for it with the remote control's setting off): its control socket and token in
  `BRULION_APP_SOCKET` / `BRULION_APP_SHARE_TOKEN` of its environment alone, with which it follows the
  conversations this app runs — its agents, Code mode's threads — and steers them for the phone, as
  the user (`ChatControl`'s `chatLive`, `chatAnswer`, `chatSend` with mode steer, `chatStart` in a
  worktree; docs/CLI.md "AI agents for other devices" → "The app's own conversations").
* **Who answers**: an agent is offered among the partners (`acp:<id>`, its name, "AI agent") and the
  conversation's page keeps it (`partner: acp:claude`, docs/SYNTAX.md §14); an agent not set up on
  this device fails the turn with what to do. It may answer every new conversation: Settings →
  Extensions → AI → New conversations answered by ("AI chat" → "Who answers a new one").
* **Its options** (`chatAgentOptions` under the conversation's row, `chatNewAgentOptions` before a
  new one's first message; `ChatAgentOptions.qml`): a chip per option of the session — its model,
  mode, effort … (`configOptions`; an agent's older `modes` and `models` as options of their own) —
  that says what it sets and its choice now ("Model: Opus 5.5", "Effort: high", "Mode: auto"), with
  an icon of its kind (`cpu`, `brain`, `shield-check`, `zap`; others `sliders-horizontal`) and a
  tooltip on what it changes; the known kinds (ACP's `category`: model, mode, thought_level, speed)
  and the common values (low … max, on, off, auto, default, Claude Code's permission modes) in the
  interface's language, others as the agent wrote them. Its menu (`chatAgentChoices`, titled by the
  option — `chatAgentChoicesTitle` —, `chatAgentChoice`: name, description or group, ✓) sets
  another (`session/set_config_option`, `session/set_mode`, `session/set_model`). A bool, and a
  select of just "Off" and "On" (any order; also true/false, enabled/disabled, yes/no:
  `agentOptionRow()` gives `onOff`, `onValue`, `offValue`, `isOn`) — Claude Code's "Fast mode" —, is
  a switch chip: the option's name, highlighted while on, a click toggles it (no bare "Off"). A choice is remembered for the agent (`AgentConfig::options`, with the options
  its last session offered, `known`) and set on every new session before its first prompt — so a
  model the account cannot use is chosen once (Codex on a ChatGPT Free plan). After them the
  session's usage (`chatAgentUsage`: "4.3 % of the context · 0.21 USD", the agent's `usage_update`)
  and the subscription's limits (below).
* **Its limits** (`chatAgentLimits`, `ChatAgentLimits.qml`; `Workspace.chats.limits`,
  `src/app/shell/agentlimits.h`; core `agentlimits/`): for an agent **of this computer** that is
  Claude Code or Codex — known by its command (`agentlimits::providerOf()`: the presets,
  `claude-agent-acp`, `claude-code-acp`, `codex-acp`, a program named `claude` or `codex`) or by the
  name its program gave in `initialize` (`WorkspaceAgents::programName()`) — how much of the
  signed-in account's windows is used: a pill "5 h: 12 % · week: 68 %" (a month where neither is:
  Codex Free), in the warning colours above 80 % and the danger colours at 100 % of any window; its
  tooltip the plan, each window — the five hours, the week, a model's own week ("Week, Opus", Codex's
  "GPT-5.3-Codex-Spark, 5-hour window"), Claude's extra usage this month — with its reset in local
  time ("resets at 18:40", "resets Fri 08:00", "resets 2 Dec 09:00") and when they were read; a click
  opens them as a menu (`chatAgentLimitsMenu`, `chatAgentLimitsWindow`) with **Refresh**
  (`chatAgentLimitsRefresh`). Also on the agent's row in Settings → AI agents (`acpAgentLimits`).
  Read when shown and every five minutes while shown, **at most every five minutes per account**
  (`agentlimits::LimitsReader`; an account is its sign-in file: two agents of one share a reading),
  Refresh at once; a reading that needed no request (no sign-in file, an expired token) is tried
  again the next time. Claude: `GET https://api.anthropic.com/api/oauth/usage` with the access token
  of `${CLAUDE_CONFIG_DIR:-~/.claude}/.credentials.json` (`claudeAiOauth.accessToken`, its
  `expiresAt`, `subscriptionType`), `anthropic-beta: oauth-2025-04-20` and Claude Code's user agent;
  Codex: `GET https://chatgpt.com/backend-api/wham/usage` with `tokens.access_token` of
  `${CODEX_HOME:-~/.codex}/auth.json` and `Chatgpt-Account-Id` (`tokens.account_id`) — the folders as
  the agent's command sets them (`VAR=… prog`, `env VAR=… prog`), else the app's environment. The
  token is read for each request, kept only in memory for it (registered with the event log's
  redaction meanwhile: `logging::ScopedSecret`), sent only to that provider's host over https (the
  transport follows no redirect to another server), never refreshed (that would rotate the agent's
  own sign-in) and never logged — the log says "claude limits read: 4 windows" or why not. What to do
  when they cannot be shown, in the pill's few words ("Limits: not signed in", "Limits: sign in
  again", "Limits: offline", "Limits: not available") and the tooltip's sentence: run `claude` and
  `/login`, or `codex login`, in a terminal; an API key's sign-in has no limits; an expired token is
  renewed by using the agent once; asked too often, try again in a few minutes. Not for an agent on
  another computer (its sign-in is that computer's) nor where no programs run. Tests: `tst_agentlimits`
  (recorded answers, malformed ones, the requests, the reader's rhythm), `tst_shell_agentlimits`.
* **Its usage** (`Workspace.chats.usage`, `src/app/shell/agentusage.h`; core `agentusage/`; T3 Code's
  usage history, ccusage's sources): for an agent **of this computer** that is Claude Code or Codex
  (known as for its limits), what it used — tokens by kind (input, output, cache writes for five
  minutes and an hour, cache reads, reasoning as part of the output), requests and what the same
  tokens would cost **on the API** — read from the agent's own session logs on this computer, never
  sent anywhere: Claude Code's `${CLAUDE_CONFIG_DIR:-~/.claude}/projects/<folder>/*.jsonl` (and
  `~/.config/claude/projects` without the variable; the subagents' files below a session too: an
  assistant line's `message.usage`, `message.model`, `cwd`, `costUSD` when the log has one, `speed`
  "fast"), each message once by `message.id` + `requestId` (it is logged again while it streams and
  when a session is resumed into a file of its own); Codex's `${CODEX_HOME:-~/.codex}/sessions/YYYY/MM/DD/rollout-*.jsonl`
  and `archived_sessions`, also compressed — `rollout-*.jsonl.zst`: Codex compresses a rollout
  untouched for a week (zstd level 3, `codex-rs/rollout/src/compression.rs`) when its experimental
  feature `local_thread_store_compression` is on (off by default from 0.140 to 0.170) or an
  app-server client asks with the experimental `rollout/compress`, and makes it plain again before
  it appends to it; read through its frames (the vendored zstd 1.5.7 decoder, `3rdparty/zstd`;
  `agentusage/logstream_p.h`: a frame's window at most 16 MiB, at most 1 GiB of text, a broken or
  cut frame ending the file as one skipped line) (a rollout once by its name, wherever it is and
  however it is kept: archived or compressed later it stays one file of the cache — nothing counted
  again, only what was appended read —, both files of the moment Codex changes it once;
  `session_meta`'s `cwd`, `turn_context`'s
  `model`; since Codex 0.154 a `token_usage_record` per API response, counted once by its
  `response_id` — a fork's copies too —, and then the rollout's `token_count` events not: they
  leave out the responses of a turn's own agents; before, `token_count`'s `last_token_usage` — or
  the difference of the cumulative `total_token_usage` — where its totals moved: a repeat with the
  same totals counts nothing, a fork's copied counts once by their totals; `cached_input_tokens` is
  part of `input_tokens`, the priority tier of `thread_settings_applied` is fast mode) — the folders as the agent's command
  sets them, else the app's environment, as for the limits. The cost comes from a price table in
  one place (`src/core/agentusage/prices.cpp`, dated `PricesAsOf`: Anthropic's and OpenAI's prices
  per million tokens, the long-context tiers above 200 K / 272 K prompt tokens, fast mode's factor;
  a `costUSD` in the log wins; a model the table does not know counts its tokens and its cost "not
  known"; Claude Code's `<synthetic>` messages cost nothing; a name the provider's backend resolves
  itself is priced as the model the provider's own program sends for an API key — Codex's automatic
  approval review, whose rollouts name `codex-auto-review` with a ChatGPT sign-in, as `gpt-5.6-luna`,
  what Codex asks for with an API key: `codex-rs/model-provider/src/provider.rs`; the history keeps
  the log's name). Per day in local time, per model, per
  project folder (the session's `cwd`). Shown on the agent's row in Settings → Extensions → AI
  agents — its line under the command, "Today 1.2 M tokens · ≈ 4.10 USD · 30 days ≈ 61 USD" ("Today
  nothing · 30 days ≈ 61 USD", "No usage yet" without logs), and **Usage…** (`acpAgentUsage`,
  `AgentUsageButton.qml`) opening `agentUsageDialog` (`AgentUsageDialog.qml`): "Usage of Claude
  Code", where the logs are, the period (`agentUsagePeriod`: 7 / 30 / 90 days), what the bars show
  (`agentUsageMeasure`: tokens or cost), which projects (`agentUsageScope`: all projects or this
  notebook only — its folder and below), the totals (`agentUsageTotal`: "12 M tokens · ≈ 61 USD · 340
  requests"; "≥ 61 USD" when a model has no price), when they were read (`agentUsageChecked`: "Read
  at 14:30"), a bar per day (`agentUsageChart`, `agentUsageBar`: today's in the accent colour, a
  date under every 7th — every 14th in 90 days, each day in 7 —, the day's use on hover and for
  screen readers: "Tue 24 Nov: 1.2 M tokens · ≈ 4.10 USD"), a row per model (`agentUsageModel`: its
  cost, "input 1.2 M · output 300 K · cache write 50 K · cache read 4 M · reasoning 20 K · 120
  requests"), the models without a price (`agentUsageUnpriced`) and the sentence that the cost is
  the API's price of the same tokens — a subscription pays nothing more for them
  (`agentUsageNote`); **Refresh** (`agentUsageRefresh`). The chat's limits pill ends its tooltip with
  today's use ("Used today: 1.2 M tokens · ≈ 4.10 USD at API prices"). Read when shown and every
  five minutes while shown, **at most every five minutes per log source** (`agentusage::UsageReader`:
  a source is a provider and its folders, so two agents of one account share a reading), Refresh
  at once, always on the reader's own thread (one reading at a time, cancelled when the app
  quits). The logs are read line by line, at most 4 MiB a line (a longer one — a picture in a tool
  result — is skipped), only lines that can carry usage parsed, a last line still being written
  left for the next reading, malformed lines skipped and counted, no symlink followed out of the
  folders. An incremental cache in the app's cache folder (`agent-usage/<provider>-<hash>.json`,
  JSON, versioned with the price table's date, written atomically for the owner only, rebuilt when
  malformed): per file its size, time, the bytes read, its first 4 KiB's hash (a file that shrank
  or whose start changed is read again from the beginning), a Codex rollout's state, the keys of
  the messages counted and its use per quarter of an hour (UTC quarters: every time zone's day is
  whole quarters, so the days follow the computer's zone, DST too). A log that is gone keeps its
  use in the cache — Claude Code deletes sessions after 30 days; the history keeps 400 —, so the
  history outlives the agent's own clean-up. Not for an agent on another computer (its logs are
  that computer's) nor where no programs run. `brulion-cli acp usage` reads the same (docs/CLI.md
  "What the agents used"). Tests: `tst_agentusage` (synthetic logs of both agents: a streamed
  message, a resumed session, cumulative Codex totals, Codex's response records, a fork, a truncated last line, malformed and
  huge lines, an unknown model, the cache reading only what was appended, a file that shrank or
  was rewritten, a gone file, days across DST and in a +05:45 zone, the reader's rhythm; an
  automatic review's real lines, anonymised; compressed rollouts made by `zstd` 1.5.7 —
  `tests/core/agentusage/fixtures/make-fixtures.sh`: as Codex writes them, with a checksum, two
  frames, cut, broken, a window too large, a line too long —, one compressed, archived or made plain
  again later, both files at once, the text's limit; each way a cache entry cannot be trusted, an
  unreadable log, one gone for longer than the history, a line exactly at the limit, what the cache
  keeps coming back as it was),
  `tst_shell_agentusage`, `tst_cli_acp_usage`, `tst_shell_agents` `realAgentUsageCounted` (a real
  agent's turn counted from its own logs — Claude Code (Haiku), 2026-11-28), the shell self-test's `checkAgentUsage` (Claude
  Code's preset with synthetic logs: the row's line, Usage…, 30 and 7 bars, this notebook only).
* **Slash commands**: while the field holds "/" and a name being typed, the agent's commands that
  match are chips above it (`chatCommands`, `chatCommand`: `available_commands_update` of the
  session, or of the agent's last one); a tap writes "/name " — the agent reads it as it is.
* **On another computer**: the stream started for the agent's first turn (`POST streams`; one per
  agent, the computer ends an older one of this device), its client offering no files or
  terminals (the computer serves them), the session's `cwd` and the context naming that computer's
  folder (`AgentConfig::root`, from its info), no MCP server from here (the computer gives its own).
  Its tools there reach this app all the same (docs/CLI.md "The app's live tools", core
  `acp/apptunnel.h`): the stream says this app answers them (`features: [app]`, where
  `WorkspaceAgents::setAppControl()` is set — the desktop and Android, not the browser), and each
  session's request names a handle of its grants and what it may do (`brulion/app` in its `_meta`):
  with "Let it change the app" the live tools — a grant of its process, made in process
  (`RemoteControl::localGrant()`, named "Claude Code on raspberry-pi-4") — and the chat's tools of
  its conversation (a chat grant made so too; "Agents delegate" below); a sub-conversation's agent
  never the live ones. A call comes over the stream (`_brulion/app`) and is answered in process
  (`RemoteControl::dispatch()`: no socket — Android has none —; the same checks, journal, live
  indicator and revert as a local agent's; never `quit`, a screenshot as the picture itself). The
  context names the app as the one on this device ("the Brulion app on the user's device
  “Pixel 9”"). While a turn runs on Android its foreground service keeps the app answering with the
  screen off; once the app is gone the computer fails the calls at once ("not connected now").
  What the agent says waits there while the phone sleeps; while a turn of it runs (or one this
  device lost is fetched: below), Android runs a foreground service (`WorkspaceAgents::remoteWorking()`
  → `platform::setAgentWork()`, docs/ANDROID.md "An AI agent on another computer"), so the stream
  goes on with the screen off and Brulion behind another app: a quiet ongoing notification
  (`WorkspaceAgents::remoteWork()`: "Claude Code is answering…", "2 AI agents are answering…", or
  the first question waiting — "Claude Code asks: Write Dom.md" —; under it "On raspberry-pi-4 ·
  <the conversation>") whose **Stop** stops those turns (`WorkspaceChats::stopRemoteAgents`) and
  whose tap brings Brulion to the front. Its ends in words, after "The AI agent
  “…” stopped: " (`WorkspaceAgents::remoteEndText`): unpaired ("pair it again"), the computer
  started again ("try again"), not reached for 15 minutes ("is it on, and is this device in its
  network?"), the agent not started there, the agent's own end; a sign-in in a terminal is named
  as one on that computer (on NixOS `sudo brulion-acp login …`). The next turn starts a new
  stream.
* **A turn this device lost**: the computer goes on with a turn when the phone is gone and keeps
  how it ended (docs/CLI.md "Turns are kept"). The device's record of the conversation's session
  (LocalStore `acp/sessions`) marks a turn of an agent on another computer as running (`pending`:
  the conversation's messages when it began; `turn`: the turn's id, sent in its prompt's `_meta` as
  `brulion/turn`) until it ends here. When the notebook is ready (or
  the agents change), a conversation so marked whose newest message is still the one asked
  (`WorkspaceAgents::recoverable()` — Android ended the app meanwhile) runs again
  (`WorkspaceChats::recoverAgentTurns` → `WorkspaceAgents::recover`): "Claude Code is still working
  on raspberry-pi-4…" while `acp::fetchRemoteTurn` long-polls the computer, then the answer is
  written as if the phone had been there (its text, else its tool calls in words and their
  titles), the
  session's record goes on (the next message resumes it), and the foreground service runs
  meanwhile as for a turn. The same when the stream of a running turn ends (the computer not
  reached for 15 minutes, or it gave the stream up). Stop asks the computer to stop it
  (`acp::cancelRemoteTurn`). A question the agent asked meanwhile — also one this device showed
  before Android ended it — waits there (docs/CLI.md "Questions wait for the device"): the turn
  brings it (`acp::RemoteTurn::question`), and it is the conversation's question as any (its title,
  the diff, the agent's options; "… on raspberry-pi-4 waits for your answer…"), answered there
  (`acp::answerRemoteQuestion`; not sent: asked again, "Your answer did not reach …"). One nobody
  answered in time (an hour by default: `acp serve --question-wait`) is answered "cancelled" there:
  "… stopped on raspberry-pi-4 without an answer: a question it asked got no answer from this device
  in time. Ask again." (an older computer answers it so at once, "… a question it asked meanwhile is
  answered “cancelled”…"); a computer whose last turn of the session has another id never got the question
  ("… did not get the question: Brulion was ended before it was sent. Ask again."); one started
  meanwhile knows no turn: "The answer of Claude Code was lost … Ask again.".
* **Its program** (`shell/workspaceagents.h`, the protocol `src/core/acp/acp.h`): one process per
  agent, started for its first turn in the notebook's folder (the command split as a shell would,
  the app's environment; on Windows a batch file — `npx` is `npx.cmd` — through `cmd.exe /c`) and
  kept while the notebook is open; several conversations answer at once, a session each. With
  Brulion's tools on, each session gets `brulion-cli mcp --vault <folder>` as an MCP server
  (`brulion-cli` next to the app, else on PATH; none found: no tools). With "Let it change the
  app" (`AgentConfig::appAccess`) also `--app-control`, and the process a grant of the running
  app (`RemoteControl::grant()` through `WorkspaceAgents::setAppGrants()`, desktop builds): its
  token and the agent's name go to the MCP server in session/new's `env` (`BRULION_APP_TOKEN`,
  `BRULION_APP_CLIENT`; never in its arguments), so its `brulion_app_*` tools reach the app — also
  while the remote control's setting is off — and change it live (docs/CLI.md "Live changes"); the
  context says so, every change shows in the top bar's live indicator under the agent's name, and
  the grant ends when the process does. Every session also gets the chat's own grant for its
  conversation ("Agents delegate" below). (An agent on another computer: its grants are made in
  process instead, its tools reach the app through that computer — "On another computer" above.) What it writes to its
  standard error goes to the log; when it ends, its turns fail with its last line ("stopped: it
  ended with code 1: …") and the next turn starts it again. A changed program or a removed agent
  stops; a notebook closed stops them all.
* **What the app serves it**: the notebook's files (`fs/read_text_file` — its lines, `line` and
  `limit` — and `fs/write_text_file`) through the Workspace, so a page it changes is seen by the
  editor at once and kept in the page's history — only files of the notebook's folder (a path
  outside it or a link out of it is refused), never an encrypted page written; and terminals
  (`terminal/create`, `output`, `wait_for_exit`, `kill`, `release`): the command runs as a program of
  the app in the folder it names (the notebook's by default), its output kept to the agent's limit
  (1 MiB by default, cut at a character), at most 32 at once; they end with the agent.
* **A turn**: a new session's first prompt says what the agent needs to know (English, the agent
  reads it: `WorkspaceAgents::context()` — the notebook's folder, the conversation's page it must not
  change, the page it is about, Brulion's MCP server and, with a grant, its live tools), then the page it is about — its text as an
  embedded resource where the agent takes one, else a `resource_link` —, the conversation so far as
  one text (`transcript()`: 🧑 and 🤖 messages, from `chat::firstSent()` with the network's budget)
  and the newest message; a session that goes on gets only the newest message. Pictures go as
  pictures where the agent takes them (`Workspace::aiPicture()`, scaled and written again), else as
  links to their files. The answer as it is written is the agent's message chunks (another message
  or a tool call between them starts a new paragraph), its thoughts the thought chunks
  (`chatThoughts`), what it does the title of its tool call in progress (`chatStatus`: "Read
  Dom.md"; "Claude Code is working…" before). Under the answer what the turn does (`chatActivity`,
  `ChatActivity.qml`: `ChatTurn.tools` and `ChatTurn.plan` — the plan, a step a line with its state
  (`chatPlanEntry`); a summary line of the tool calls in words (`chatToolsSummary`:
  `WorkspaceChats.toolSummary()`, `shell/chattools.h` — a part per kind in the order the turn first
  did them, "Read 3 files · ran 2 commands · edited 1 file", at most three named and the rest "N
  other steps", failed calls counted apart, "1 step failed", plural forms through the translation)
  that opens and closes their list: the newest four (`chatToolCall`, "N earlier steps" opens them
  all) with an icon of their state — closed, the calls running and the failed ones stay — and,
  under the one running or the last, its output's last lines —
  a terminal's as it comes (`chatToolOutput`, plain monospace text) —, a file's change behind "Show
  the change" as a diff (`chatToolDiffToggle`, `chatToolDiff`: BDiffView, unified); the first change
  at once, those after it ten times a second at most). The turn's answer is written as the AI's message with its
  time, as any partner's; a turn of tool calls without words writes their summary line and their
  titles as a list ("Edited 1 file", then "- Edit Dom.md"; an agent on another computer keeps only
  the titles: "Took 2 steps").
* **The work log** (T3 Code's "Worked for …"): once a turn with tool calls or a plan answered, what
  it did stays above its answer — one folded line (`chatWorkLog`, ChatActivity with `finished`:
  "Worked for 25 s · Edited 1 file · ran 1 command") that opens its plan and its calls as they
  ended, their output and "Show the change". `WorkspaceChats.workLog(path, number)` (the answer's
  number in the page, from 1; `workLogSerial`), kept in the notebook's device state (LocalStore
  `chat/worklog`, not in the page: another device shows the answer alone) — 50 turns a
  conversation, 200 conversations, the newest 200 calls of a turn, an output's last 4000 characters,
  a change's texts while they are 40 000 characters at most (`workLogEntry()`). A command's output
  that is one fenced block (Claude Code's "```console") shows without the fence
  (`WorkspaceAgents::plainOutput()`). "Hide all" hides the line too.
* **Tool calls in chat** (Settings → Extensions → AI agents, `chatToolCallsRow` /
  `chatToolCallsSetting`; `AppSettings.chatToolCalls`, key `ai/chatToolCalls`, this device's — T3
  Code's setting): **Show all** (`all`, the default: the list open), **Hide finished**
  (`hideFinished`: the list closed — a call shows while it runs and stays only if it failed; the
  summary line still opens it) and **Hide all** (`hideAll`: no summary, only failed calls, and the
  status line says only "Claude Code is working…" instead of the call's title). Questions, failures
  and the plan always show.
* **Sessions after a restart**: the session's id is kept in the notebook's device state (LocalStore
  `acp/sessions`: {conversation → agent, session, how many messages it has seen}); the
  conversation's next turn after the app started again resumes it (`session/resume`, else
  `session/load` — its replayed history not heard — where the agent offers them; a new session with
  the conversation so far when that fails). A conversation that changed meanwhile — another
  partner answered, a message written on another device — gets a new session with all of it; one
  renamed takes its session along — renamed while its turn runs, once the turn ends
  (`WorkspaceAgents::followRename`); renamed after Stop while the stopped turn's answer is still on
  its way, at once: `acp::Client` finds a session by a serial of its own, never by the
  conversation's name an answer was asked under, so that answer ends the session's turn under the
  new name and the next turn goes on in the same session (an answer for a session closed meanwhile
  is never taken for a new one's); one deleted stops its turn and closes its session
  (`session/close` where the agent offers it).
* **Questions**: `session/request_permission` is the conversation's question (`chatQuestion`): its
  title "Claude Code asks: Write Dom.md", what it would do under it — a file's change as a diff
  (`chatQuestionDiff`: BDiffView, unified; `WorkspaceChats.questionDiff()`), else the tool's input as
  JSON (plain text, at most 4 000 characters) — and the agent's options as buttons, in the agent's
  words ("Allow", "Always allow", "Reject"). When no chat view shows the conversation, a
  toast says it with **Open** (`WorkspaceChats.questionWaiting`, AppController). A sign-in the agent
  asks for (`auth_required`) is a question too: its methods as options and Cancel; a method run in a
  terminal only is named in the failure ("run claude /login in a terminal, then try again").
* **Stop** (`WorkspaceChats.stop`): `session/cancel`, a question waiting answered "cancelled", the
  turn's commands ended, nothing more of that turn; a message sent meanwhile waits for the agent to
  end the cancelled turn.
* **Approvals** (`chatApprovals` in the conversation's row, `chatNewApprovals` before a new one's
  first message; menu `chatApprovalsMenu`; as T3 Code's permission modes): how the agent's
  questions are answered — **Ask first** (`ask`, each one shown), **Allow note edits** (`edits`: a
  change of a file — the call's kind `edit` or `move` — and reads go by themselves, commands and the
  rest ask) or **Allow everything** (`all`); a plan to approve and a sign-in always ask
  (`WorkspaceAgents::autoAnswer()`: the question's first `allow_once` option, else `allow_always`;
  what was allowed shows as the turn's status, "Allowed by your approvals: Write Zgoda.md"). Kept
  per conversation in the notebook's device state (LocalStore `chat/approvals`, following a rename)
  — never in the page, which syncs from anywhere —; a new conversation gets the mode chosen last.
* **A plan to approve** (Claude Code leaving its plan mode: a `switch_mode` call with the plan in
  its input — acp::Permission::plan): the question shows the plan rendered as a message
  (`chatQuestionPlan`, `WorkspaceChats.questionPlan()` through ChatMessageModel.render — escaped,
  never the agent's HTML) instead of the input as JSON, with **Save the plan as a page**
  (`chatSavePlan`: "Plan – <the conversation's name>.md" in the notebook's top folder, a link back
  to the conversation under it — `savePlan()`), and the agent's options ("Yes", "No, keep planning").
* **What a turn changed** (`chatChanges`, `ChatChanges.qml`, under the last answer once the turn
  ended; as T3 Code's turn diff): "Changed 3 files" with **Undo these changes** (`chatChangesUndo`)
  and × (`chatChangesDismiss`: kept as they are), a row per file (`chatChange`: its path — a tap
  opens it —, changed / new / deleted, **Show the change** with a unified diff of its text before the
  turn and now, `chatChangeDiff`). Followed by `WorkspaceAgents` while an agent of this device
  answers (`m_tracking`): a file is kept as it was the moment the app first sees it named — a
  question about its change (before the agent can make it), a tool call of the kinds edit, move or
  delete naming it (`locations`, its diff's path: Claude Code changes files with tools of its own,
  never asking the app) or the agent's `fs/write_text_file`; a snapshot that already holds the
  call's change (the app saw the call late) is turned back with the call's diff
  (`WorkspaceAgents::textBefore()`), or marked "not known before". A shell command names no file
  (Claude Code's Bash, Codex's commands: an execute call): while a turn runs one, another program's
  changes of the notebook — files made, changed, deleted or renamed — are that turn's, their text
  before taken from the index before it takes the new one (`WorkspaceAgents::notesChanging()`,
  called by `Workspace::onVaultChanged()`; a file that is no note: not known before), and the
  turn's end looks at the notebook once more (`Vault::rescan()`: the file watcher waits a moment,
  and a quick agent ends its turn first). Undo writes each file back as it was (a file the turn
  made goes to the trash) unless it changed since — those stay, said in the toast
  (`undoChanges()`). In memory until the conversation's next turn starts; not for an agent on
  another computer (its files are there), nor for Brulion's own MCP tools in a turn that runs no
  command (another process: their writes are in the pages' history).
* Tests: `tst_acp` (the protocol, the agent played), `tst_shell_agents` (a real Workspace and a real
  program that speaks ACP: `tests/app/shell/fakeacpagent.cpp`; `realAgent` and `realAgentThread` —
  a thread in a real git project, tried with Claude Code and Codex — with a real one,
  `BRULION_REAL_ACP_AGENT`, `BRULION_REAL_ACP_OPTION`; `realAgentWakesParent` with two —
  `BRULION_REAL_ACP_CHILD`, `BRULION_REAL_ACP_CHILD_OPTION` —: the parent's agent starts a
  sub-conversation with wake through the real brulion-cli and a control socket of the test's own, the
  child answers, the parent is woken and answers with it; tried Claude Code → Codex and Codex →
  Claude Code on 2026-11-27; `realAgentChatFeatures` — a message queued while a turn runs, one
  steered in, a side chat, a fork, "Edit from here" — and `realAgentUndoesItsChanges` — a file the
  turn changed with the agent's editing tool or a shell command, put back by Undo —, tried with
  Claude Code (Haiku) and Codex (gpt-6-luna; its default gpt-6-sol is refused on a ChatGPT
  account) on 2026-11-28; `realAgentPlanApproved` — Claude Code in its plan mode asks with a plan
  (its options "Yes, manually approve edits", "Yes, and use auto mode", "Yes, and bypass
  permissions", "No, keep planning"), saved as a page, then carried out — on 2026-11-28; the played
  agent's shell command: `shellChangesUndone`), `tst_shell_chattools` (the calls in words,
  English and Polish plural forms), the shell self-test's `checkChatDrawings` (the drawings of an
  answer, ChatActivity under each "Tool calls in chat") and `checkCodeMode` (Code mode: a project and
  its thread, the header, the sidebar, the Changes panel, the file picker, find in the thread).

### Agents delegate

An AI agent that answers a conversation may give another partner of the chat a task — Claude Code
asks Codex for a second opinion, hands a research subtask to another agent, has the assistant write a
note — and get the answer back, as T3 Code's subagents: a **sub-conversation**, an ordinary
conversation page whose frontmatter names the one it came from (`parent: "[[AI/…]]"`, docs/SYNTAX.md
§14). Desktop: every agent of this computer with Brulion's tools gets `--app-chat` and, for each of
its conversations, a grant of the app's chat alone (`WorkspaceAgents::setChatGrants()` →
`RemoteControl::chatGrant()`, the token in its tools' environment as `BRULION_APP_CHAT_TOKEN`), so its
`brulion_app_chat_*` tools (docs/CLI.md "Conversations") reach the running app — also while the remote
control's setting is off; the context tells it in a sentence (`WorkspaceAgents::context()`). The app
names itself to those tools (`WorkspaceAgents::setAppInstance()`: its single-instance key as
`BRULION_APP_INSTANCE`), as an agent may not pass its environment on — Codex starts its MCP servers
with `HOME`, `PATH`, `LANG` … alone, so its tools found no `XDG_RUNTIME_DIR` and looked for another
socket than the app's. The built-in assistant's tools do not change (docs/FINETUNE.md).

* **In the parent conversation** (`chatLineage`, `ChatLineageRow.qml` under the header): "Sub-conversations:"
  and a chip per sub-conversation (`chatSubConversation`: its name · who answers, an icon of its
  state — answered, answering, waiting for the model on the device, waiting for your answer
  (highlighted), failed (the danger tint) — and the state in its tooltip); a click opens it. In a
  sub-conversation a chip "From: …" (`chatParent`) opens the conversation it came from.
  `Workspace.chats.lineage` (`shell/chatlineage.h`): `children(path, serial)`, `parentOf()`,
  `depthOf()`, `isDescendant()`, `runningChildren()` — the parents read from the pages once and
  again when a conversation's page changes (another device's too).
* **Questions**: a sub-conversation's agent asks as any conversation's does — its question card in
  it, a toast with Open when no view shows it; the parent's agent learns from its wait that the
  sub-conversation waits for the user (`waiting_for_user`) and tells them.
* **A sub-conversation that wakes its parent** (T3 Code's "notify the parent"): `chatStart` / `chatSend` with
  `wake` (docs/CLI.md "Conversations": `--wake`) — the asking agent ends its turn instead of waiting, and
  when the sub-conversation's turn ends (answered, failed, stopped by the user) or stops to ask the user, the
  conversation it came from — the agent's own (its grant's), a script's the sub-conversation's parent — gets
  a message (`chat::wakeMessage`, docs/SYNTAX.md §14: "↩ Sub-conversation [[AI/…]] answered:" and the answer
  quoted, 4 000 characters at most) and its partner a turn: `WorkspaceChats::setWake()` (kept in the
  notebook's device state `chat/wake`, following renames of both, armed before the turn starts so one that
  fails at once wakes it too), the message going through the parent's queue — after its running turn, or
  the turn waiting for the model on the device; kept over a restart (`chat/queued`). A question wakes it and
  the turn's end once more; the caller's `chatStop` takes the wake away (who stops knows), a sub-conversation
  deleted wakes nobody, a parent deleted meanwhile is not woken. At most `MaxWakesPerHour` (10) wakes of one
  conversation an hour (the workspace's clock): the next one, and any for an **archived** parent, is held in
  its queue for the user with a line saying why (Resume sends it) — two agents cannot wake each other for
  ever. The sub-conversation's chip shows "↩" and its tooltip "will wake this conversation when its turn
  ends" (`children()`'s `wakes`); in the parent the message has **From a sub-conversation: …**
  (`chatWakeHeader`, `chatWakeHeaderText`: a tap opens it; `ChatMessageModel`'s `wakeFrom` / `wakeName` of
  `chat::wakeSource`) above its bubble — an ordinary message of the user's side otherwise. The agents'
  context and the MCP server's instructions say so.
* **Limits** (`ChatControl`, `shell/chatcontrol.h`): an agent starts sub-conversations of its own
  conversation only, sends to and stops only its own sub-conversations, at most **three levels** deep
  (one that deep gets no chat grant) and **four** of one conversation running at once; a
  sub-conversation's agent never gets the live tools ("Let it change the app"), whatever its setting.
  An agent on another computer delegates the same way: its chat grant is made in process and its
  `brulion_app_chat_*` calls come over its stream ("On another computer" above).
* The protocol's chat methods (`chats`, `chatRead`, `chatStart`, `chatSend`, `chatWait`, `chatStop`,
  `chatPartners`, `chatSchedule*`, `chatRequestSecret`) are `ChatControl`'s: GUI-free over `WorkspaceChats`
  (`send(…, partner, parent)`, `stateOf()`: idle, running, queued, asking, failed), the desktop's
  `RemoteControl` passing them on with who asks.

### Scheduled conversations

**Settings → Extensions → Scheduled conversations** (`ShellChatSchedules.qml`, shown with a notebook of
the user's open; `chatSchedulesSettings`): a message sent to a partner of the chat when a rule
written in words says — "codziennie o 7:30", "w poniedziałki o 9:00", "w dni robocze o 8", "co 3
godziny", "jutro o 8" (once), "every day at 7:30" (core `schedule/schedule.h`, through the dates
module's natural dates and recurrence rules) —, each run a new conversation or the next message of
one conversation (T3 Code's scheduled tasks).

* A row per schedule (`chatScheduleRow`): its title, then its rule in words, who answers, "a new
  conversation each time" or "one conversation", its next run ("next: jutro 07:30"; "off", "done"
  for a single one that ran), who set it up when an agent did, and why its last run did not start;
  **Run now** (`chatScheduleRun`: it runs at once and the conversation opens; the next run counts
  from now), **Edit…** (`chatScheduleEdit`) and **Delete** (`chatScheduleDelete`); **Add…**
  (`chatScheduleAdd`). Empty: "None yet." (`chatSchedulesEmpty`).
* The dialog (`chatScheduleDialog`): the message (`chatSchedulePrompt`), when (`chatScheduleWhen`:
  checked as it is typed — the rule in words and its first run under it, `chatScheduleWhenText`,
  or why not: "At most every 15 minutes…", "That time has passed.", "Not understood…"), who answers
  (`chatSchedulePartner`, the chat's partners), each time a new conversation or the same one
  (`chatScheduleDestination`), a name (`chatScheduleTitle`; default the message's first words) and
  On (`chatScheduleEnabled`).
* **Kept on this device** (AppSettings `ai/schedules/<the notebook's id>`, JSON; at most 50) —
  never in the notebook, which syncs from anywhere: a synced file must not start agents; none for
  the documentation. `Workspace.chats.schedules` (`shell/chatschedules.h`): `rows`, `check()`,
  `add()`, `update()`, `save()` (the dialog's, with its webhook's token), `newHookToken()`,
  `maskedHookUrl()`, `copyHookUrl()`, `hookOnlyText()`, `setEnabled()`, `remove()`, `runNow()`,
  `runHook()`, `lastError()`; AppController gives it its storage when a notebook opens.
* **Runs while the app runs** (the desktop tray keeps it running; on Android while it is open, with
  partners that answer there): looked at every half minute and when the notebook is ready, so a run
  missed while the app was closed runs once at its next start — not once per missed occurrence. A
  toast says "Scheduled conversation “…” started" with Open. Skipped, with why: a partner that does
  not answer on this device, its one conversation still answering the last run.
* Times are local wall-clock time: a time the spring skips runs at the first minute after the gap,
  one the autumn repeats runs once; an interval counts the hours that pass.
* Agents and scripts manage them too (`brulion-cli app chat schedule list | add | set | remove |
  run`, docs/CLI.md "Conversations"); one an AI agent sets up names it and its conversation, its runs
  are sub-conversations of it, and the agent changes only those.
* **Webhooks** (T3 Code's webhook triggers; core `webhook`, `shell/hookserver.h`, `src/cli/hooks.cpp`):
  a schedule can also be started by a request from elsewhere — a GitHub webhook, Home Assistant, a cron
  job on another machine, iOS Shortcuts. In its dialog **Also start it by a webhook**
  (`chatScheduleHook`, off by default) gives it an address of its own, `<address>/hooks/<token>` (256
  random bits, base64url), shown masked ("http://127.0.0.1:8791/hooks/Xk3d…", `chatScheduleHookUrl`)
  with **Copy** (`chatScheduleHookCopy`: all of it onto the clipboard as a secret — clipboard managers
  keep it out of their history) and **New address** (`chatScheduleHookNew`: the old one stops working
  once saved); a warning line while webhooks are off. With a webhook **When** may stay empty: then only
  the webhook (and Run now) start it — "only by its webhook" / "tylko przez webhook" in the rule's place
  (`When::Kind::None`). The row says "Webhook" (a link icon, `chatScheduleWebhook`) and when it last
  started one. A request — `POST` only: a link preview or a browser's prefetch must never start a run —
  starts it as Run now does (a new conversation, or the next message of its one), the message the
  schedule's own and under it a sentence that what follows came from outside and is data, never
  instructions, and the body in a fenced block (JSON indented, a form's fields one a line, text as it
  is: UTF-8 only, at most 1 MiB, cut at 16 KiB — docs/CLI.md "For agents: note text is data"). Its
  timed rule does not move (the run is `lastHook`). Answers: 202 `{"conversation": "<name>"}`; 404 for
  an unknown token, one that is off, or a schedule of a notebook not open (the same answer); 429 with
  Retry-After more than once a minute per schedule, or while its one conversation still answers (as
  the scheduler skips); 400 (not UTF-8, another charset, control characters), 413 (over 1 MiB), 405
  (not POST), 503 (the app not ready, its partner not answering here). Nothing of the notebook in any
  answer. The tokens are kept with the schedules on this device (AppSettings `ai/schedules/<notebook
  id>`), never in the notebook, registered with the event log's redaction and masked in the server's
  log; only the user sets one up, in Settings — the remote control's schedules say `webhook: true`
  and never give or take a token, so agents managing schedules through the CLI cannot read or make
  one.
* **Settings → Extensions → Scheduled conversations → Webhooks** (`chatHooksSettings`, desktop):
  **Webhooks** (`chatHooksSwitch`, `AppSettings.chatHooks`, key `ai/hooks`, off by default; its line
  what the server does: "On at http://127.0.0.1:8791 · this computer only: put your own reverse proxy
  or Tailscale Funnel in front…", "Starting…", "Stopped: …"), **Where it listens**
  (`chatHooksAddress`: this computer only — 127.0.0.1, the default — or, while tailscaled runs here
  (core `tailscale`), **Tailnet**: this computer's tailnet address, reached as
  `http://<its MagicDNS name>:<port>` by the user's own devices; key `ai/hooksAddress`), **Port**
  (`chatHooksPort`, 8791; `ai/hooksPort`) and **Public address** (`chatHooksPublicUrl`,
  `ai/hooksPublicUrl`: what the hooks' addresses start with when the user's own reverse proxy or
  `tailscale funnel --bg 8791` makes it reachable from the internet). On, the app runs `brulion-cli
  app hooks serve` (`HookServer` over `ServerProcess`, as "Share agents with the phone" runs `acp
  serve`: started again after a crash, stopped with its standard input; every line of its log in the
  event log, tokens masked) with a grant of the remote control of its own (`BRULION_APP_HOOK_TOKEN`:
  hello and `chatHook` alone, which nothing else may call) — docs/CLI.md "Webhooks" for why a process
  of its own. Every hook is in the event log ("Webhook of scheduled conversation s… started it (json,
  1234 bytes)", or why not), never its token. Tests: `tst_webhook`, `tst_schedule`, `tst_ipc_control`
  (`hookGrants`), `tst_cli_process` (`appHooks`), `tst_shell_agents` (`webhooks`).

## AI in the editor

The **writing assistant** (while the AI assistant is on — `BlockEditor.writingAvailable`; never in
reading mode): what the user points at — the selected text of the block being edited (none
selected: its whole text; a continuation: the text up to the caret), else the selected blocks, else
the selected block; in source mode its selection, else the paragraph at the caret — **rewritten**
after a preset or the user's own words, **continued**, or **summed up**, by the model of Settings →
Extensions → AI (the model on the device too). Nothing is sent but on the user's action, and nothing
changes in the note but on Replace / Insert.

* **Where**: the palette — **AI: Rewrite…** (`ai.rewrite`, **Ctrl+Shift+A**), **AI: Continue
  writing** (`ai.continue`), **AI: Summarize** (`ai.summarize`): uiAction `writing:<action>` to the
  page's editor or the journal day being written in (`BlockEditor.startWriting(action, preset)`);
  the edited text's menu (a right-click: `editTextRewrite`, `editTextContinue`,
  `editTextSummarize`); the block menu (`blockMenuRewrite`, …: the block with its children, or the
  selection it is part of); on touch UIs the editing toolbar's ✨ (`toolbarAi`; in source mode
  `toolbarSourceAi`) and the selection tools' ✨ (`selectionAi`), which open `writingMenu` above the
  bar: Rewrite…, Shorter, Clearer, Fix spelling and grammar, Translate to English (Polish in an
  English UI), Continue writing, Summarize.
* **The dialog** (`qml/editor/WritingDialog.qml`, `writingDialog`: a centred card, a bottom sheet on
  a phone — modal, so the text it works on stays as it was): "Rewrite…" without a preset shows the
  text it will rewrite (`writingOriginal`) and the presets (`writingPresets`, chips
  `writingPreset_<id>`: shorter, clearer, grammar, formal, casual, english, polish) and a field for
  the user's own words (`writingInstruction`, `writingAsk`) — nothing is asked until one is chosen;
  then, and for the other actions at once, what the model writes as it comes (`writingResult`,
  `writingResultText`: the Markdown that would go in, plain text), "The AI is writing…" (`writingStatus`;
  "Waiting for the model on this device…" before the first word), **Stop** (`writingStop`: what came
  stays, "Stopped: what was written so far."), **Try again** (`writingRetry`: another answer), and a
  rewrite's change word by word (`writingView`: Changes / New text; `writingChanges`: removed words
  struck through in the danger colour, added ones on the success tint — `WritingSession.changesHtml`,
  our own escaped HTML). **Replace** (a rewrite) / **Insert** (a continuation, a summary) is one undo
  step: the block's text through `PageController.pasteAndEdit()` (a result of several blocks becomes
  blocks, as a paste; editing goes on after it), the selected blocks through `replaceSelection()`, a
  summary or a continuation of blocks after them through `insertAfterBlock()` (their nesting kept, at
  the block's depth: a sibling list item in an outline); in source mode one step of the text's own
  undo (`EditorInput.replaceRange`). A text that changed meanwhile (a sync) is left alone, with a
  notice. Under a written result **Report response…** (`writingReport`) opens the chat's report
  dialog ("AI chat": `ShellAiReportDialog`) for it — its box adds "the text it worked on" (the
  instruction and the text sent; off), who wrote it is `WritingSession.modelName()`, the place
  `writing/<action>/<preset>`; no Hide: the result is in no note until Replace / Insert —, and once
  sent the row says "Reported response" (`writingReported`) until the next answer.
* `WritingSession` (`src/app/shell/writingsession.h`, QML `WritingSession { workspace: Workspace }`):
  `startInline` / `startBlocks` / `startSource` take the pieces (the text, the context around it, where
  the result goes), `rewriteWith()`, `retry()`, `stop()`, `clear()`, `state` ("" → "waiting" →
  "writing" → "done" | "stopped" | "failed"), `result` (cleaned for the note as it comes),
  `change()` — the request and the answer are core `writing/writing.h` (pure: `request()`,
  `clean()`, `edit()`, `apply()`, `diff()`, tests in `tests/core/writing`), sent through
  `WorkspaceExtensions::sendAi()` (the settings and client of b.ai, counted in `aiPending`: the top
  bar's ✨ spins).

## Statistics and focus mode

**Word count** (`stats/textstats.h`, `PageController.pageStats()` / `selectionStats(text)`): the
words, characters (with and without spaces) and reading time (200 words a minute, rounded, at
least 1) of the open page — its source in source mode — and, while text in a block or whole blocks
are selected, of the selection. What is counted is what a reader sees: Markdown syntax, the
frontmatter, `key:: value` properties, block ids, task and reminder tokens, comments, embeds,
pictures, block references and HTML tags are left out, a link counts as its label, fenced code
(code, query, calc, Mermaid) and display formulas not at all; a word is a run between whitespace
with a letter or digit in it (hyphenated words, "m.in.", "3,14", addresses are one word; a dash or
emoji alone none; a no-break space separates words except between digits) — the header of
`textstats.h` has the rules, `tst_stats` the cases. Shown:

* in the status bar (wide desktop windows, "Window layout" → Status bar; `PageView.statusText`) for
  the focused pane's page, else in the bottom-right corner of PageView (`pageStatsFooter`: the
  other pane of a split view, focus mode, touch UIs; wide windows, not in the side panel, not over
  a docked touch toolbar); both follow Settings → Editor → "Word count" (`AppSettings.wordCountShown`,
  on by default): "123 words · 820 characters · 1 min read", "Selection: …" while something is
  selected, the corner's with the characters without spaces in its tooltip and accessible name;
* at the top of the page's menu (`pageMenuStats`, disabled item, every width) — counted when the
  menu opens.

Counting is not per keystroke: the footer counts once the page has not changed for 400 ms plus
1 ms per 100 characters (at most 3 s), the selection 150 ms after it changed; with the footer hidden
nothing is counted until the menu opens. The page's counts are taken on a worker thread
(`PageController.countPageStats()` → `pageStatsCounted`: the text is taken on the GUI thread, a
result for an earlier request or another page is dropped; single-threaded builds count from the
event loop) — a 5000-block page's count is some 330 M instructions, which a posted call still put
before the page's first frame. A page that opens is counted once (a `Later`: the counts becoming
available and the footer shown arrive together and asked for two whole counts before); the menu
counts synchronously as it opens.
The journal stream shows no count (a day opened as a page does).

**Focus mode** (`AppSettings.focusMode`, `ui/focusMode`, a device setting kept across restarts;
palette `view.focus`, F8; Settings → Editor): where an editor shows a page or the journal
(routes page, journal, journals) the shell hides the sidebar (and the drawer's edge swipe), the
top bar, the update banner, the side panel and the bottom navigation; the view fills the window
and the editor centres its text column (`BlockEditor.contentX`). The content keeps clear of the
system bars itself (the SafeArea's top, the navigation bar while not typing). It ends with F8, Esc
(AppShell's `Keys` — only once the view did not take it: suggestions, editing, a block selection
come first), the Back key (before the history) and the quiet corner button `focusModeExit`
(`minimize` icon, clearer under the pointer). Other views keep their bars while the setting is on.

**Typewriter scrolling** (`AppSettings.typewriterScrolling`, palette `view.typewriter`): the
caret's line is kept in the middle of the view — `BlockEditor.typewriter` (`centerInView()`
instead of `ensureVisible()` for the caret; PageView adds half the view's height under the last
block, `bottomSpace`), the source editor (`SourceEditor.keepCaretVisible`, a bottom margin of half
the view) and the journal stream (its `centerInView(item, rect)` as the days' `scroller`), as far
as the content goes (the first lines of a page stay where they are).

## Reading mode

**Reading mode** (`AppSettings.readingMode`, `ui/readingMode`, a device setting kept across
restarts; palette `view.reading` "Reading mode" (the eye) / "Leave reading mode" (the pencil),
Ctrl+Shift+E; the top bar's eye button — on pages and the journal, every width, checked while it is on —, the page menu "⋯",
Settings → Editor) shows pages and the journal read-only, like Obsidian's
reading view: a tap or click follows links, opens embeds, pictures, PDFs and query results, folds
and zooms, but never starts editing, so on a phone no keyboard comes up while reading. PageView and
the journal days bind `PageController.readOnly` to it; the editor reads it as `BlockEditor.readOnly`.

* **What still works**: following links and chips' menus that change nothing (a day link's
  Open / Copy, a link's and a tag's menu), selecting blocks (Esc, Shift+↑/↓, a drag, a long press,
  Ctrl+A) and copying them (Ctrl+C, the block menu's Copy and Copy link — a link that would add a
  block id says why: "readingMode" —, the selection tools' Copy), folding and zooming, find in the
  note (Ctrl+F; the matches are selected and flashed, not edited), a table's filter row, the word
  count, version history, export and print. A drawing opens as a picture (its view's "Edit
  drawing" opens the editor). The page menu's Rename… / Move to… / Move to trash are deliberate
  dialogs and stay.
* **What is not shown**: the block menu's Cut, Row ▸ / Column ▸, Insert picture / drawing,
  Indent / Outdent, Move and Delete; the selection tools' Cut, Delete, indenting, moving and task
  status; a token chip's changes (its menu opens only for a day link); a table's "+ Row" /
  "+ Column"; the properties panel's fields and add row (values are shown, "Copy value" stays);
  the header's note mode toggle, "Start writing", the cover's tools, the chips' "+" and "Remove";
  the find bar's replace row (Ctrl+H opens the bar without it). Text shows an arrow cursor, not an
  I-beam, and a tap under the last block adds nothing.
* **What says why**: whatever would still change the note — a tap on text, a checkbox (also in
  query results and embeds), the task status menu, Enter / Delete / Tab / Alt+↑↓ / Ctrl+X / Ctrl+V
  on a selection, Ctrl+Z, Ctrl+E, the page menu's note mode, icon, cover, property and source items
  (shown unavailable), an empty journal day's prompt ("Nothing written on this day.") — gives the
  toast "Reading mode is on: the note is not changed" with **Edit**, which leaves reading mode
  (`BlockEditor.readOnlyLeaveRequested`). The toast comes at most once in 3 s.
* **The guard is the controller's**: every `PageController` edit (blocks, tasks, tokens, tables,
  properties, the mode key, pastes, drops, pictures, undo / redo, source mode, a new block id,
  `replaceTextRanges`, `setTaskStatusAt` / `toggleTaskAt` of another note, `StreamFind.replaceAll`)
  refuses while `readOnly` and emits `editRefused()` — the editor's toast —, so a path the UI
  missed still writes nothing. Turning the mode on ends the editing and source mode, their text
  applied and saved as usual; a change from outside (sync, another editor) still reloads the page.
  Tests: `tst_editor_readonly` (every edit refused, reading, switching), `tst_editor_qml_reading`
  (`_touch`; real clicks and taps: `--editor-script readingtest`), `tst_settings`.

## Custom shortcuts

`ShortcutMap` (`shell/shortcutmap.h`, QML singleton): the actions are the palette itself
(`palette`, Ctrl+K) and every command of `CommandModel` with its default shortcut
(`CommandModel::shortcutActions()`; commands without one can get one: "Sync now",
"Tasks"…). Custom shortcuts are a device setting (AppSettings group `shortcuts`: id = portable
text, "" = none) — one key chord with Ctrl, Alt or Meta, or a function key. `check(id, sequence)`
refuses, with a message in the UI language: not one chord ("invalid"), a key that types or moves
in text ("typing"), the keys of editing text and blocks, the drawing editor, the PDF viewer, switching tabs (Ctrl+1…9, Ctrl+PgUp, Ctrl+PgDown) and
menus, and Ctrl+Alt with a letter Windows' AltGr makes Polish ("reserved"); another action's
shortcut is a "conflict" — `assign(id, sequence, true)` moves it, the other action is left
without one. `reset(id)` brings the default back (whoever took it meanwhile loses it),
`resetAll()` all of them. Stored values are read defensively: no string (an INI list), malformed
or not allowed is ignored; two actions on one key — the earlier custom one keeps it, a default
taken by a custom one gives way.

**Scopes**: an action is everywhere (`Action::scope` "") or Code mode's only ("code": its commands,
the shown project's scripts — docs/CODE.md "Shortcuts"). Shortcuts are unique within a scope; a Code
mode action may have the key of one that is everywhere (it wins in Code mode: the other's binding is
`shadowed` and AppShell turns it off there) and keys reserved for what Code mode never shows (the
drawing editor, the PDF viewer, the canvas, the tabs' Ctrl+1…9, the AI chat's Alt+1…9:
`reservedIn(n, "code")`); `actionIn(sequence, scope)`, `scopeOf(id)`, `checkIn(scope, sequence,
except)` (a script being edited, no action yet). Settings → Keyboard shortcuts lists Code mode's
commands as "Code mode: …".

Applied everywhere through `ShortcutMap.sequences[id]` / `bindings`: AppShell makes one
`Shortcut` per action (`runShortcut(id)`: the shell's popups directly, the rest
`CommandModel.run(id)`), except find, replace, Markdown source and a page property, which PageView
and JournalsView bind to their own Shortcuts (their state decides); the editor's own Ctrl+E and
Ctrl+Shift+C are handled by it only while they are those commands' shortcuts
(`BlockEditor.sourceModeShortcut` / `copyRefShortcut`, bound by the views; another key is a
window Shortcut). The palette shows the shortcut each command has now, and so do the top bar's
and menus' hints (BIconButton / BMenuItem `shortcut`, always `ShortcutMap.sequences[id]`, never
a literal of a command's default: the tab bar's +, ×, its menu's Close tab and the phone's tab
switcher — `tab.new`, `tab.close` —, a page window's ← / → — `nav.back`, `nav.forward` —, the
find bar's Replace — `page.replace` —, Add property in the properties' header, the page header's
"+" menu and More — `page.addProperty` —, the page menu's Rename… — `page.rename` —, the
notebook switcher's Brulion Documentation — `vault.docs` —, More's Command palette on narrow
desktop windows — `palette`); a hint for a fixed key is a literal (the tree's Move to trash:
Del). Key caps show the arrow keys as arrows (`TextHelper.shortcutKeys`: "Alt+Up" is Alt ↑, in
every language; display only). On touch UIs no hint shows (`BKbd`).

Settings → **Keyboard shortcuts** (`ShellShortcutSettings`, also the palette's
`settings.shortcuts`): every action with its key caps ("No shortcut" for none), a search over
titles, ids and keys (case and diacritics ignored), **Change** — the row waits for the next key
(an invisible recorder takes the keyboard and every ShortcutOverride, so no window shortcut fires;
a modifier alone waits; Esc or Back cancel; focus leaving ends it) —, the refusal or the conflict
under the row with **Assign here**, ✕ (no shortcut), ↺ (the default, shown as "Default: …"
for a custom one) and **Restore all** with a confirmation. Above the list the **Vim keys** switch
(the same device setting as Settings → Editor; "Vim mode" below).

## Vim mode

Vim's keys in the block editor and in source mode — a device setting, off by default
(`AppSettings.vimMode`, key `editor/vimMode`): Settings → Editor → **Vim keys**, the same switch at
the top of Settings → Keyboard shortcuts, the palette's `view.vim` ("Vim keys" / "Turn off Vim
keys"). Off, nothing of it runs: no key is looked at, the editor is as it was. Reading mode has none
(nothing is edited). It is meant for a hardware keyboard: in normal mode the text takes no typing,
so a phone's on-screen keyboard only types in insert mode.

**Pieces.** The core module `vim` (`src/core/vim/vim.h`) is the whole of Vim's logic: a GUI-free
state machine (`vim::Engine`) reading keys and moving the caret and editing text through an
abstract `vim::Buffer` of lines and columns (a column is a UTF-16 index; the caret steps over
surrogate pairs and combining marks), registers and the last search / change / command line in a
`vim::Shared` (one per app: yank in one page, put in another), `vim::TextBuffer` the plain buffer of
its tests. The app (`src/app/editor`): `VimSession` (QML type, one per `BlockEditor`:
`BlockEditor.vimSession`) owns an engine over `VimPageBuffer` (the page's blocks,
`PageController`'s operations in `pagecontroller_vim.cpp`) or, while source mode's TextEdit exists,
`VimSourceBuffer` (its `QTextDocument`); its properties are what the UI shows (`mode`, `modeLabel`,
`pending`, `commandLine`, `message`, `status`).

**Keys.** BlockEditor gives a key to Vim before its own handling: an inline editor's
(`handleEditorKey`), the list's while no block is edited or blocks are selected
(`handleVimListKey`; a block selection made with the mouse or Shift+↓ becomes Vim's visual line
selection), source mode's (`handleVimSourceKey`; a text selected with the mouse in normal mode
becomes the visual selection). In insert mode Vim takes only Esc and Ctrl+[ — every other key is
the editor's as without Vim (Enter splits the block, the completion popups, Tab indents). In the
command modes (normal, visual, replace) it takes every key that types a character and the keys Vim
has (Esc, Enter, Backspace, Delete, the arrows, Home, End, PgUp, PgDn, Ctrl+R, Ctrl+D, Ctrl+U, Ctrl+A, Ctrl+X); of
the rest only the editor's shortcuts go on (`vimPasses`: Ctrl, Alt or Meta with anything, Tab /
Shift+Tab, F-keys, Shift+arrows), nothing that would type. The TextEdit is read-only in the command
modes (`InlineEditor.vimCommands`; no input method composes into it) with a block caret over the
character (`vimCaretBox`; visual mode shows the TextEdit's own selection) — switched with the caret
kept, as a TextEdit moves its caret to the end whenever `readOnly` changes. A modifier pressed
alone (Shift for a capital, AltGr for ą) or a dead key keeps a pending command.

**The page drawn by Atrament** (docs/ATRAMENT.md M2 step 2, `src/app/editor/atrament/pagevim.cpp`):
the edited block's keys go to the same `VimSession` by `handleEditorKey`'s rules; the field is
read-only in the command modes (no input method) with a steady block caret — an underline in
replace mode —, the bar in insert mode; Vim's visual selection is the field's own; zt / zz / zb
scroll Atrament's list and H M L read its rows; VimStatus is fed as for the QML editor.

**Polish keyboards.** Commands are read from the character a key types, not from its place (Vim's
way): on the Polish programmer's layout the letters and signs are where Vim has them, and the AltGr
letters (ą ć ę ł ń ó ś ź ż) are no commands — only arguments (`fą`, `tę`, `rź`, `/żółw`, `ma`…).
Windows' AltGr (Ctrl+Alt with the character) counts as typing; keypad digits are counts. On the
typist's (QWERTZ) layout y and z are where that layout has them.

**The page as lines.** `VimPageBuffer`: Vim's lines are the lines of the visible blocks' edit texts
in order (a block with a line break has several; a folded block is one line and what it hides goes
with it; the frontmatter is no line — the properties panel edits it; a table is one line of no
text: the caret on it selects the table, its text is not Vim's to change, `dd` `yy` `p` take it
whole). So `j` `k` and counts go across blocks keeping the caret's column, `w` `b` `e` `}` `%` and
searches run on into the next block, `gg` `G` `:N` are the first, last, N-th line of the page. Edits
map onto the controller (each one step, `PageController::joinUndoSteps()` joins the steps of one
change of Vim — an operator, `x`, `p`, an insert from `i` to Esc with what was typed — into one
undo step, so `u`, Ctrl+Z and the toolbar's Undo take it back whole):

* text within a block is text (`x` `r` `~` `cw` `:s`… cost what a keystroke costs: typing's own
  path); across blocks the blocks between go and the two are joined as Backspace at the second
  one's start joins them (`J`, `vjd`, `d}` mid-line; the last block's children follow the first);
* a line break typed or put into a text block makes a new block after it (Enter in insert mode
  splits as always; `p` of text with line breaks, `:s/,/\r/`), into code, quotes, callouts a line;
* `dd` (and `d` of whole lines: `dj`, `dip`, `V…d`) removes the block — an open one alone (its
  children move up, as the text would read without its line), a folded one with what it hides; a
  line of a code block, quote, callout is a line of its text; a page left without blocks gets an
  empty one (Vim's one empty line);
* `yy` keeps the blocks' Markdown (markers, tasks, the nesting relative to the first; a folded
  block with its children) besides the lines' text, and `p` / `P` put blocks after the block and its
  children / before it at its depth (into the text of a code block below / above the line). A
  block id (`^id`) of this page stays on the put copy only after a delete (`dd` `p` is a move;
  `yy` `p` is a copy without it — `((id))` stays one block's);
* `o` / `O` open a block as Enter at the block's end / start does (a task goes on as a task; a
  block with children gets its first child), a line in a code block or a middle line of a text;
* `>` `<` indent and outdent as Tab and Shift+Tab (with children, all or nothing for a range; a
  document indents list items only); lines of code get or lose a tab;
* a list item's text never starts with blanks (they would join its marker): `cw` on its first word
  puts the blank after what is typed again.

The visual selection is the edited block's text selection while it stays in one block; across
blocks the caret's block stays edited and the rows of the selection are drawn as selected (a band:
`PageController::showLineSelection()`, the model's selected rows — no block selection of the
controller, so no selection tools, no subtrees: `V` on an open parent takes its line only). `za`
`zo` `zc` `zR` `zM` fold and unfold blocks, `zt` `zz` `zb` scroll the caret's block. In the
journal stream every day is a buffer of its own (`j` `k` stop at its edges, as ↑ ↓ do). Insert mode
ends when no block is edited any more.

**Source mode** (`VimSourceBuffer`): Vim over the page's text as in Vim itself; `>` `<` indent with
a tab, or the spaces the text's list items are indented with. One change of Vim is one step of `u`
/ Ctrl+R there too: the document's own history steps a change made are undone and redone together
(a change starts with an empty step, so its typing never merges into the one before; Ctrl+Z in the
text walks the document's steps one by one).

**What it shows.** The mode, the command being typed (Vim's showcmd), the command line (`:` `/`
`?` typed into the engine itself: Backspace, Ctrl+U, Ctrl+W, ↑ ↓ history; Esc cancels) and the
last message ("Not found: …", "3 substitutions on 2 lines", "Already at the oldest change" — in the
UI's language, `VimSession::messageText()`) — on desktops at the left of the status bar
(`statusBarVim`, from `PageView.statusVim` / `JournalsView.statusVim` through `ViewTraits`),
elsewhere (phones, the side panel, focus mode, a page's window without the bar) in the view's
bottom left corner (`VimStatus`, above a docked touch toolbar). The mode badge: normal quiet,
insert in the accent's colour, visual amber, replace red. `:q` (also `:wq`, `:x`, `ZZ`) goes back
in the tab's history, at its start closes the tab when there are others, else goes home; `:q!` too
(notes are saved as they are typed: nothing is lost); `:w` saves now.

| Keys | |
|---|---|
| `i` `a` `I` `A` `o` `O` `gi`, Insert; Esc / Ctrl+[ | insert mode; back to normal mode (the caret steps back) |
| `R` (Backspace restores) | replace mode |
| `v` `V`, `o` (other end), `gv` | visual, visual line |
| `h` `j` `k` `l`, arrows, Space, Backspace, Enter, `+` `-` `_` | characters and lines (counts) |
| `w` `W` `b` `B` `e` `E` `ge` `gE` | words (Polish letters are word characters) |
| `0` `^` `$` `g_` `|`, Home, End | in the line |
| `gg` `G` `N%` `{` `}` `%` | the page, paragraphs (empty lines), the matching bracket |
| `f` `F` `t` `T` + a character, `;` `,` | in the line |
| `/` `?` `n` `N` `*` `#` (`g*` `g#`) | search: plain text, smart case (a capital letter: exact case), wraps around |
| Ctrl+D / Ctrl+U, PgDn / PgUp | half a screen / a screen |
| `m` + `a`–`z`; `` ` `` `'` + mark, `` `` `` `''` `` `. `` `` `^ `` `'<` `'>` | marks (a block's mark follows the block) |
| `d` `c` `y` `>` `<` `g~` `gu` `gU` + motion or text object; doubled (`dd` `cc` `yy` `>>` `<<` `g~~` `guu` `gUU`) for lines | operators (counts before and after) |
| `iw` `aw` `iW` `aW` `ip` `ap` `i"` `a"` `i'` `a'` `` i` `` `` a` `` `i(` `a(` `ib` `ab` `i[` `a[` `i{` `a{` `iB` `aB` `i<` `a<` | text objects (also in visual mode) |
| `x` `X` Delete `D` `C` `s` `S` `Y` `r` `~` `J` `gJ` `p` `P` `gp` `gP` | the usual |
| `u`, Ctrl+R, `.` (with a new count) | undo, redo, repeat the last change |
| `"a`–`"z` (`"A`–`"Z` append), `""` `"0`–`"9` `"-` `"_`, `"+` `"*` (the system clipboard), `".` `"/` `":` | registers |
| `:w` `:q` `:q!` `:wq` `:x` `ZZ` `ZQ`, `:N` `:$`, `:[range]d` `y` `>` `<` `j`, `:u` `:red`, `:noh` | the command line (ranges: `%` `.` `$` N `'a` `'<,'>` `+N` `-N`) |
| `:[range]s/old/new/[g i I n]`, `:s`, `&` | substitute plain text (never a pattern): `&` in the new text is the match, `\r` a line break, any delimiter |
| `za` `zo` `zc` `zR` `zM`, `zt` `zz` `zb` | folds, scrolling |
| `(` `)`, `is` `as` (also in visual mode) | sentences (below) |
| `H` `M` `L` (`3H` `3L`: the third line from the top / bottom) | the first non-blank of the view's top, middle, bottom line; a linewise motion and a jump (below) |
| Ctrl+A, Ctrl+X (counts; visual mode: each line's first number in the selection, `g` Ctrl+A / `g` Ctrl+X: each one more) | add to / subtract from a number (below) |
| `q{a-z A-Z 0-9}` … `q`, `@{reg}` `@@` `@:` (counts) | macros (below) |
| `:[range]g/text/cmd`, `:[range]v/text/cmd` (`:g!`), `:[range]norm[al] keys` | a command on the lines that have the text (or have not), keys typed on lines (below) |

**Sentences.** A sentence ends at `.` `!` `?` followed by the end of the line or a blank (closing
`)` `]` `"` `'` `”` `»` may stand between); an empty line is a boundary and is a stop of `(` `)`
(a run of them is one); a line break inside a paragraph is not one — Vim's rules, so in an outline
page of blocks without full stops a sentence is the whole run of blocks up to an empty line. Notes
need three exceptions, kept simple (`sentences.cpp`): a period after one of a short list of
abbreviations (`np` `tzn` `tj` `itd` `itp` `tzw` `ok` `dr` `prof` `mgr` `inż` `ul` `al` `pl` `godz`
`tel` `nr` `wg` `zob` `por` `ang` `ks` `św` `płk` `gen` `mjr` `kpt` `im` `etc` `vs` `mr` `mrs` `ms`
`jr` `sr` `st` `cf` `eg` `ie` `fig`) is no end; a period after the number of a list item at the
line's start (`1. `) is none; and a period whose next word, on the same line, starts with a small
letter is none ("ok. 5 kg. i tak"). `?` and `!` always end one. `)` goes to the start of the next
sentence (at the last one: the text's last character), `(` to the start of this one, else of the
previous one; both are jumps and motions of operators (`d)`, `c(`). `is` is the sentence without
the blanks after it (the blanks between sentences are an item of their own for `is`), `as` with the
blanks after it — none after the last one of a paragraph: the blanks before it; the line break
after a sentence belongs to `as` only when another sentence follows in the paragraph. A count takes
more sentences (`2das`). `is` in visual mode does not grow an existing selection yet.

**Numbers.** Ctrl+A / Ctrl+X (`numbers.cpp`) work on the first number that ends after the caret
(under it or after it, on its line): decimal, hexadecimal `0x1F` / `0X1f` and binary `0b101` (Vim's
`nrformats=bin,hex`). A `-` is a sign unless a letter, digit or `_` stands before it — unlike Vim,
in `2026-11-23` the day is 23 and `foo-1` is 1 (notes are full of dates). Leading zeros keep the
width (`009` → `010`), hexadecimal and binary keep their width and the case of their digits and
wrap around at 64 bits, decimal numbers stop at the 64-bit limits. The caret ends on the number's
last character; the change is repeatable with `.` (a new count too) and one undo step. In normal
and visual mode these keys are Vim's: select all and cut give way (BlockEditor's `vimOwnsShortcut`),
in insert and replace mode they are the editor's.

**H M L.** The view is the editor's: `Buffer::viewLines()` asks `VimSession`, which emits
`viewRequested()` and gets the answer through `setView(first, last)` before the signal returns —
BlockEditor answers with the first and last row drawn in the viewport (`vimReportView()`, through
`rowAt()`; a journal day: what its scroller shows of it), SourceEditor with the text positions at
the Flickable's top and bottom (`reportVimView()`). A buffer that does not know (no answer) leaves
the keys without effect. `H` / `L` stop at the other end of the view, `M` is the middle line
(`(last - first) / 2` from the top).

**Macros.** `q{reg}` records what the engine is fed (`Engine::feed`: key events, so a macro runs on
whatever text is there when it plays, not on the text it was recorded on — typing in insert mode
included), the `q` that ends it is dropped; the keys of a macro played by `@` are not recorded
again. `vim::Shared` keeps them (`Shared::recorded()`) and writes their notation (`keysToNotation()`:
`ciwNowe<Esc>`, `<C-r>`, `<lt>`) into the register as its text, so `"ap` shows a macro and a yanked
or edited text runs too (`@a` plays the keys while the register still holds their notation, else
`keysFromNotation(text)`; a linewise register ends with Enter); `qA` appends. `@@` is the last one,
`@:` the last command line, counts repeat. Playing is the engine's queue: a macro that calls
itself (`qaq`, `qa…@aq`, `@a`) is not recursion of the stack but keys put in front of the queue
as Vim's typeahead, and it ends where Vim's does — at the first failing command (a motion that
cannot move, a search or `:s` that finds nothing, an empty register, a number that is not there…);
one that never fails stops after 400000 keys ("Macro stopped"). All it does is one undo step
(the buffer's `beginChange()` around it). Typing played in insert mode is the engine's own:
text, Enter (a new block: as the editor's Enter without its list continuation, completion or
auto-pairing), Backspace, Delete, ←, →, Home, End; other keys are skipped. The status shows
"recording @a" (`VimSession.recording`) until `q`; turning the setting off ends it.

**`:g` and `:normal`.** `:g/text/cmd` — text is plain (smart case, as `/`), any delimiter but a
letter, digit, blank, `"` or `|`; no `cmd` goes to the last matching line; `cmd` is any command of
the command line, also `normal`; `:g!` and `:v` take the lines without the text; a range limits it
(default the whole page). The matching lines are marked first (`Buffer::lineAnchor()`: block uids
on a page, text cursors at the lines' ends in source mode — a removed line is gone from the list)
and each gets the command with the caret at its start; the whole of it is one undo step; `:g`
does not nest. `:[range]normal keys` types keys in normal mode on each line of the range (the
current line without one) as Vim does, line numbers counted once; keys are written as in Vim's
mappings (`<Esc>` `<CR>` `<C-a>`: typed as those letters in the command line), an unfinished command
or insert ends as Esc would end it, a failing line does not stop the next ones.

**Clashes with the app's keys.** The app's shortcuts keep working in normal mode where Vim has
nothing on them (Ctrl+K, Ctrl+O, Ctrl+F, Ctrl+E, Ctrl+Z / Ctrl+Y, Ctrl+C / Ctrl+V, Ctrl+A, Tab,
Alt+↑ …). Vim's in the command modes: Ctrl+R (redo; also over a custom shortcut given Ctrl+R —
it works in insert mode and elsewhere), Ctrl+D / Ctrl+U, Ctrl+A / Ctrl+X (numbers: the editor's select all
and cut give way in the command modes, not in insert and replace mode), Ctrl+[ (Esc), and Ctrl+W, Ctrl+U,
Ctrl+H in the command line. Esc in normal mode with nothing to cancel does not select the block (`V` does): it
goes on to the view — focus mode ends. Ctrl+V (block visual mode), Ctrl+O, Ctrl+I (the jump list),
Ctrl+F, Ctrl+E, Ctrl+N, Ctrl+P keep the app's meaning, not Vim's. Not there: block visual mode,
patterns in `/` and `:s` (a safe pattern engine first), the jump list — TODO.md has them.

Tests: `tst_vim` (the engine on a TextBuffer: 650+ key sequences with Polish text, counts, undo
grouping, the dot repeat, registers and the clipboard, the command line, numbers, sentences, H M L,
macros, `:g` and `:normal`, boundaries and malformed input), `tst_editor_vim` (VimSession over a PageController and over a source TextEdit: blocks as
lines, one undo step per change, visual selections, folds, tables and the frontmatter, block ids,
registers across pages, `:w` `:q`, macros, `:g` and sentences over blocks and over source text, H M L
with the view's answer, a key's cost on a 5000-block page against a keystroke's), the
shell self-test (`checkVimKeys`: real keys into a page and its source on the phone and the desktop
layout).

## Navigation

`AppController` keeps the route history of every tab (`navigation.h`: `RouteHistory`, routes are
`QVariantMap`s `{view, path?, line?, blockId?, date?, tag?, query?, section?, …}`; the tabs:
`tabs.h`, "Tabs, split view and windows" below); `ShellRouteView` shows the view for a route — a
tab's in its pane (`ShellTabPane`), and the side panel's route. Every view is loaded from its QML
file when it is first shown (`ShellRouteView.viewSource()`, `Loader.setSource()` with the view's
first properties — its route, and of the pane what it takes: `inSidePanel`, `paneActive`,
`covered` —, `Binding`s afterwards): `ShellRouteView` names none of them as a type, since a QML
file named in another one is loaded with it (docs/PERFORMANCE.md "Memory: only what is in use").

A built-in view an extension replaced (`b.app.replaceView`, docs/EXTENSIONS.md "Built-in views
replaced") shows the extension's view for its route: `ShellRouteView.showRoute()` shows
`Workspace.extensions.shownRoute(route)` — core's `replacedRoute()`, `{view: "extension",
extension, name, replaces, …params}` — while the route itself stays (the history, tabs, the
bottom navigation's highlight and `b.app.state()` keep `"tasks"`), and shows the route again on
`viewReplacementsChanged` (an extension loaded, switched off, live code unloaded), so a replaced
view on screen changes at once. A replaced kept view is not built ahead. `ExtensionRouteView`'s
header then has **Built-in view** (`extensionRouteBuiltin`): the route with `builtin = true`,
which shows the app's own. Tests: `tst_extensions_app` (`viewsReplaced`, `replacedRoutes`),
`tst_shell_scripting` (`builtInViewsReplaced`).

A pane keeps the views of the
phone's bottom navigation — Journal, All pages, Search, Tasks, Calendar — once it has shown them
(`ShellRouteView.keepViews`): hidden while another view is shown, each with the route it was last
shown with, and shown again at once, where it was scrolled, instead of being built anew (on a phone
building one took 0.5–1 s); a view hidden this way ends its editing first (`endEditing()`, the
touch keyboard goes). Kept while they are in use, not for good (`ViewKeeper`,
`src/app/shell/viewkeeper.h`; the limits are `MemoryPolicy`'s, `src/app/ui/memorypolicy.h`): a
pane holds the three hidden views shown most recently (`MemoryPolicy.keptViews`: a rotation
through the four views of a phone's bottom navigation never rebuilds one; with a page or another
view in between, the one shown longest ago is), lets go of all of them once no view was
switched for five minutes (`hiddenViewMs`), and when the app is away (below); a view let go is
made again when it is shown next, with its route — not where it was scrolled. The main window's
main pane also builds one of them ahead, in the background
(`ShellRouteView.preloadViews`): a moment after the notebook is ready and indexed, the view the
user went to last on this device (`ui/recentView`; none on a first start), by an asynchronous
`Loader` over several frames, hidden, with its plain route; so the first switch to it is instant
(opening one still being built finishes it at once). A view built hidden leaves the keyboard
alone: it takes it in its
`takeFocus()` when first shown (docs/PERFORMANCE.md "Switching views"); a hidden view given the
focus passes it to the view shown ("Keyboard shortcuts (desktop)" above). Other views (a page,
Graph, Settings, …) are made for their route and deleted when another view is shown; the side
panel keeps none.

**Away.** `MemoryPolicy` (a QML singleton; the app's instance follows the application's state and
its windows) says when the app is not in use: another program in front for a minute, or no window
shown — minimised, in the tray, a phone's home screen — for eight seconds (five on a phone;
twenty right after a start into the tray). It emits `letGo(hidden)` then and everything hidden
goes: the panes' kept views, the shell's closed popups and dialogs (`AppShell.letGoPopups()`: the
four built ahead and the ones made when first opened that keep nothing between two openings —
new page, rename, move, about, delete, an extension's settings and copy), the spell checker's
dictionaries, the catalogs the built-in extensions read. A quarter of a second later (the
deletions have run) the policy has the QML engine collect its garbage and drop the QML files
nothing uses any more (`trimComponentCache()`), every window drop what it cached, and — where
`releasesGraphics` is on: Linux — a window that is not shown give up its scene graph and its
graphics context, which Qt Quick builds again when the window is shown; `MemoryKeeper` then hands
the freed pages back to the system, and lets go of the program's own pages (below). Qt Quick's `Shape` does not survive that by itself (Qt 6.12:
it keeps pointers to the nodes it made and makes a path's nodes only when the path changes — the
notebook's round mark came back without its fill): the policy makes every `Shape` of the window
start over (another renderer asked for, polished, its own again). In front again, the app is in
use at once, and the popups are built ahead again. An app whose window is shown but was never in
front (a test's X server without a window manager) counts as in use. `BRULION_MEMORY_POLICY=0`
keeps and builds ahead everything as before (A/B measurements), `BRULION_AWAY_MS` sets the
delays, `BRULION_HIDDEN_VIEW_MS` the kept views' time, `BRULION_RELEASE_GRAPHICS=1` / `0` the
release of a hidden window's graphics whatever the platform. Tests: `tst_memory_policy`,
`tst_shell_viewkeeper`, the shell self-test's "0b" (kept views, the three most recent, the one
built ahead) and "0d" (away).

**Not used for a while.** The program is memory too: every page of its code and of its
libraries' that was touched once stays mapped — 160–190 MB after a start, most of it read by the
start alone. Twelve seconds after the last key, click, touch or turn of the wheel
(`MemoryPolicy.idleMs`; the first window shown counts as the first thing done), and whenever the
app is away, `MemoryKeeper.releaseAll()` lets go of those pages (`platform::releaseProgramPages()`,
Linux: the segments of the program and its libraries that are not writable and hold no page of the
process's own; the libraries OpenGL calls go through are left alone). They stay in the system's
page cache, and what the app uses next is mapped again as it is touched: opening a 5000-block
page right after takes as long as before. Nothing shown or kept goes (`idle()`, not `letGo()`).
`BRULION_IDLE_MS` sets the time (0: only when away), `BRULION_PROGRAM_PAGES=0` keeps the pages.
Tests: `tst_memory_policy` (`notUsedForAWhile`), `tst_processmemory` (`programPages`).

| Route `view` | View | Parameters |
|---|---|---|
| `journals` (home) | `JournalsView` — the stream of daily notes, "Go to day…", missed reminders banner (`ContentMissedBanner`: Open, Snooze, Done — a task's reminder only: `reminders.complete(id)` marks the task done in the note like the status menu, the shell's toast "“…” marked done" confirms it; no undo step in an open editor, as with the snooze — and dismiss) | `date?` |
| `journal` | `JournalsView`, one day (opened "as a page") | `date`, `path?` |
| `page` | `PageView` — page header (tags and aliases as chips, edited there: "Page properties"; note mode toggle — on a wide header; a small one, phones and the side panel, leaves it to the page menu "⋯", the top bar's More and the palette —, favourite, page menu with "Add property"; the page actions end the title row, and a page without one — it starts with its own "# Title" — puts them at the end of its chips' first line, the chips wrapping short of them, or on a row of their own when it has no chips; for a page named by a tag in use — with a rename at once, at every width — Tag overview is a "#" button among the page actions before the note mode toggle, Tasks with this tag is in the page menu "⋯" (also in Tag overview and every `#tag`'s menu), and its sub-tags are chips under the title; no row of buttons comes between the title and the properties), the block editor (outline or document), linked references (links and the blocks / pages tagged with a tag naming the page — "whole page" for a page tagged as a whole —; a page not written yet lists what already names it), unlinked mentions (that section is not drawn while the side panel shows them on its Links tab: "Window layout"). A weekly or monthly note (docs/SYNTAX.md §1 "Weekly and monthly notes"; `Workspace.periodicNoteOf`) has its period under the title (`periodicNav`: "Week 45 (Nov 2–8, 2026)", also the top bar's title, with the previous / next period's arrows and "This week" / "This month" when it is another one), and one not written yet starts from its period's template (`weeklyTemplate` / `monthlyTemplate`, `{{date}}` the period's first day) as a day without a note does. A page not written yet opens ready to type, except one opened from a `#tag` (`fromTag`, set by `Workspace::resolveRoute`) on a touch UI: the keyboard would cover the tagged blocks listed below, so the header's "Start writing" or a tap under it starts typing. `line` / `blockId` / `heading` scroll to that block, select it and flash it (a heading by `PageController.rowOfHeading`, docs/SYNTAX.md §2; one that is not there: a toast, the page at its top); `Workspace::resolveRoute` sends a `blockId` the page no longer has to the page holding it. A picture attachment opens in the picture view, a PDF in the PDF viewer ("PDF viewer": `pdfPage`, `from`, `fromLine`), an Obsidian base (`.base`) as its views ("Database views": `view` or `heading` its view by name; Ctrl+E its YAML read-only), any other file that is no note read-only as text | `path`, `line?`, `blockId?`, `heading?`, `open?`, `fromTag?`, `pdfPage?`, `from?`, `fromLine?`, `pdfScale?`, `view?` |
| `calendar` | `CalendarView` — Month / Week / Agenda, the day panel, add dialog; the events of other calendars in their colours, the header's calendars button, the event dialog ("Calendars" below; `{open: "event"}` opens a new event). In the month and week grids today is a red disc on its number (`accentSolid`) and the selected day a dark one (`Theme.primary` with `primaryFg`, as in `BDatePicker`; ≥ 3:1, WCAG 1.4.11 — today selected shows only the red one); no cell or header fills: the month's cells are one surface, days of other months have faint numbers (`fgFaint`, normal weight); on touch UIs with narrow cells (phones, touch tablets: under 96 px — a mouse keeps the titled pills down to 72 px, so a 1280-px desktop with the sidebar and the day panel, cells of ~91 px, lists its entries) a day shows one small icon (14 px) per kind of open entry, most urgent first (`CalendarModel` `openKinds`), in its `Theme.markColor`: overdue a red `circle-alert`, reminder or alarm an amber bell (one: four icons do not fit the cell), due a blue calendar, scheduled a violet hourglass — a shape, not only a colour; nothing for done entries, and a daily note is a short faint line under the number, not a mark (nearly every day has one: a grey dot for it drowned out the others); an alarm's pill in the month is in the reminder's tint with its alarm clock; under ~104-px cells a pill shows its title alone (the time stays in the pill's accessible name, its tooltip — also shown for an elided title — and the day panel). Done entries read as in Tasks and the editor: the "done" chip's fill and muted text with a small check, not struck out; cancelled ones are struck out with an x (the week grid's pills and the day panel's rows follow the same rule). In a month cell and in the day panel's "Plan of the day", open entries come first (a stable partition, times keep their order), so "+N more" hides finished ones first and a click on a pill opens the entry shown there. An agenda day's header is a button (`CalendarAgendaList`; a tap or click, or Enter on the list's current day — Up / Down move a focus ring from the selected day): it selects the day — its number on the grids' dark disc, today keeping its red one —, the day panel follows and phones open the day's sheet, while the list stays where it is (`CalendarModel` keeps the agenda's four weeks while the selected day is among them; ← / → go on from them); the journal button and the entries keep their own clicks. A past day of the agenda says so with colours, not a fade (at 72 % opacity its faint text fell under AA): the circle outlined (`Theme.border`), its number and the open entries' titles in `fgMuted`. The day panel's actions (Open journal / Write in journal, Add task, Add reminder) are one even list of full-width rows (icon and label, buttons for accessibility; 30 px with a mouse, 44 px on touch) under the date, the icons in line with it. The day panel's empty "Plan of the day" is a short note in line with its header, without a picture. The add dialog (`CalendarAddDialog`, "New task" / "New reminder"): a typed time ("other") applies as the date picker's does — on Enter, when the field is left, on Add, the clock button and "More options…", never per keystroke —; text that is no time keeps Add disabled at once and quietly: "Use a 24-hour time, e.g. 18:30" shows under the field only once such a text is applied, until the next keystroke (as in the date picker: a hint coming and going while "14:30" was typed changed the sheet's height); for today it opens on the next full hour once the notebook's default reminder time has passed and the quick times already past are disabled (other days keep every time: a past day can still get one); a time that has passed is said under the field ("This time has already passed", warning tone). Weekly and monthly notes (docs/SYNTAX.md §1): the month grid has a column of ISO week numbers on its left (`CalendarWeekNumber`: the ISO week of each row's middle day, also when weeks start on a Sunday; faint, a week with a note semi-bold in `accentText`; a tap / click opens the week's note through `AppController.openPeriodicNote`), the week grid has its number in the corner above the hours ("Wk" over it), the header's title opens the month's note (month view) or the week's (week view; a small book after the title when it has one, underlined on hover), and W / M open the selected day's week / month note | `date?`, `mode?` |
| `tasks` | `TasksView` — quick add, filters, sections (Overdue, Today, Tomorrow, This week, Later, No date, Done), "Show as query"; subtasks under their task (`TaskListModel.nestSubtasks`, docs/SYNTAX.md §5 "Subtasks": indented by their depth in the note's order, done ones too; one more urgent than its task's section — an overdue step of a task for later, an open step of a done task — is listed in its own section; the filters apply to them, the home screen widget lists every task on its own); a task with subtasks under it folds them with its progress chip, a chevron before "3/5" (or "2 subtasks" when none counts), and with Left / Right on its row (`TaskListModel.toggleSubtasks`: view state kept while Tasks is open, nothing written; the fold follows its task when an edit moves it in its note, the section counts keep the folded ones); a click on a task's `#tag` filters the list, Ctrl+click opens the tag's page, a long press or right-click its menu (Filter by tag, Open tag page, Tag overview, Open in side panel, Copy tag); a right-click or long press on a chip: its token's menu ("Token and link menus") | `query?` |
| `habits` | `HabitsView` — the habits (docs/SYNTAX.md §5 "Habits"; the built-in extension `nawyki`), each a card: today's check, schedule, streaks, completion rate, the month grid ("Habits" below) | `month?` ("YYYY-MM"), `open?` (`add`: Add habit's form) |
| `graph` | `GraphView` — whole / local graph (depth 1–2), toggles for tags (the "#tag" nodes of tags that name no page; a tag naming a page is an edge to it), missing pages, journals, templates. The layout is computed on a worker thread (in slices between frames in the browser) and unfolds over about a second at the display's frame rate (phones 60 fps): pan, zoom, select and drag work from the first frame, a dragged node follows the finger at once. Reduced motion: the layout is computed without animation and shown once; a fresh one shows a spinner (`graphModel.settling`) instead of a spiral that jumps — also when a filter, the local graph or the index changes before it is shown. Drawn through the scene graph (GPU) | `path?`, `depth?`, `select?` |
| `search` | `SearchView` — notes and the notebook's PDFs and pictures ("Searching attachments"), with search by meaning on also the notes closest in meaning ("Search by meaning"); results in a folder of any depth with `folder` (the tree's "Search in folder"; the route follows its renames and moves). Shown (made, built ahead and shown, or shown again), its field takes the keys with the query selected — not on touch screens (`Theme.touchUi`): tapping the tab must not bring the on-screen keyboard over the recent searches, a tap on the field does (as on every tab of the bottom navigation: none of them gives a field the focus on touch). The filters are one row; on compact windows (< 600 px) two — the scope (All / Pages / Journals / Attachments, without icons) alone across the width, then Replace…, the folder and the result count —, so they never make the view wider than a phone (one row needed ~426 px in Polish) | `query?`, `folder?` |
| `allPages` | `AllPagesView` — with `path` a folder browser: its subfolders as rows, its pages (with subfolders' or not), a breadcrumb in the top bar; follows the folder's renames and moves; a path part of a link naming a folder opens it (below) | `path?` (folder), `sort?` |
| `tags` / `tag` | `TagsView` — every tag (a row opens the tag's page, its trailing button the tag overview) / the tag overview (the pages and blocks carrying the tag, parent and sub-tags, "Open page" or "Create page", "Tasks with this tag"). A tag names a page (docs/SYNTAX.md §3): a click on `#tag` anywhere asks `Workspace::navigate({view: "tag", tag})`, which `resolveRoute()` turns into the page route (`Workspace.tagPage`); routes the shell navigates to itself (`AppController.navigate`, `--route`) and `overview: true` keep the overview | `tag?`, `overview?` |
| `sync` | `SyncView` — Overview, Syncthing, Nextcloud (live progress, history of runs), GitHub (a notebook kept in a GitHub repository: "Notebooks from GitHub" below; `{tab: "github"}` is where a notebook opened from GitHub lands), Calendars (ICS subscriptions and CalDAV accounts: "Calendars" below; `{tab: "calendars"}`), Conflicts (merge; drawings element by element). Conflicts: the list beside the resolver on wide windows; on narrow ones (< 900 px) one at a time, and a conflict opens through the route (`{tab: "conflicts", copy}`, the entry before it showing the list, also when the tab was chosen here), so the top bar's ← and Android's Back return to the list — the resolver has no ← of its own — and a resolved one goes back to it too. `copy` is read on every route change: a route without one shows the list | `tab?`, `copy?`, `run?` (open a run's details) |
| `importExport` | `ImportExportView` — import: the source apps as tiles (`ImportSourcePicker`), the chosen one's export steps (and the phone's), pick files / a folder (the system's picker, `FilePicker`; the browser's own, several files at once) / a typed path, the preview, the report. The Microsoft To Do card adds `MsTodoSignIn` above the file buttons: the Application (client) ID field, the authority under "More" (both remembered on the device), **Sign in to Microsoft**; then a well with the device code in large monospace, **Copy code**, the address as text and an **Open microsoft.com/devicelogin** button, a spinner with the progress, the minutes left and **Cancel**; an error box explains AADSTS failures; the downloaded lists go into the usual preview. In the browser only a hint (the file from Graph Explorer). The OneNote card uses the same block (`service: "onenote"`, scope Notes.Read) and, once signed in, lists the notebooks with check boxes and **Download n notebooks**; the Todoist and Airtable cards add `TokenSignIn` (the personal token in a password field, where to get it, **Download from Todoist** / **Connect to Airtable**; Airtable then a base menu and the tables as check boxes, **Download n tables**); the OneNote card has no file buttons. For a spreadsheet (CSV, Airtable) the preview adds the mapping card `ImportCsvMapping` (`ImportController.csvOptions` / `csvMapping`): A page per row / One page with the table (`BSegmented`), the title column (`IoChoiceButton`: a button with a menu of choices) or a title template, subfolders, the page-text column, a row per column — its name and a sample, the property's name (phones: a line of its own) and its type ("Detect: Number" until chosen, or Leave out) —, **Download the attachments** when cells name files on the web, the encoding, separator, header row and list separator under a toggle, the base switch, the first rows with their pages' titles and the first page as it will be written (monospace), **Back to what Brulion detected**; every change is checked again before anything is written. Export (`ExportPanel`): the format tiles (also PDF and Word: "Exporting and printing a page"), what to export (notebook / folder / page; several notes in a PDF or DOCX: each on its own page), for the HTML, PDF, Word and Pandoc formats **Pictures from the internet**: Keep addresses / Include copies (`ExportController.webPictures`, remembered on the device: `AppSettings.exportWebPictures`; with copies the progress says "Fetching pictures from the internet (1 of 3)…" and a picture that could not be fetched is a warning of the report; pictures of this device or its private network are included only from the cache — never fetched by the export —, and past 256 MB of copies the rest keep their addresses, each case one warning), for the "HTML document" **Protect with a password** (`ExportPasswordBox`: "Exporting and printing a page"), Export…, the progress card, save (the system's save picker, `FilePicker`; the browser's save picker or a download) | `tab?`, `importer?`, `files?`, … |
| `history` | `HistoryView` — versions of a page, diff, restore; each says what replaced it (your edit, another program, sync, an import, the command line — "Replaced from the command line (agent)" with a terminal icon —, a restored version) | `path` |
| `code` | `CodeView` ("Code mode" above): an AI agent's thread of a project with its changes and terminal, or a new thread | `project?`, `path?`, `new?`, `addProject?` |
| `notifications` | `NotificationsView` ("Notifications" below): what the app told the user in this notebook, newest first under a heading per day (`section`: Today, Yesterday, the date); All / Unread (`notificationsFilter`), "Mark all as read" (`notificationsMarkRead`), "Clear" (`notificationsClear`, confirmed); a row (`notificationRow`: the kind's icon, its title — semibold while unread —, three lines of its text, its time, an accent dot `notificationUnread`, × `notificationRemove`) is marked read and opens what it points to on a click or Enter (`AppController.openNotification`), Delete removes it | — |
| `trash` | `TrashView` — a row shows "Restore" (its icon on compact windows) and a "⋯" whose menu, also from a right-click or a long press on the row, has Restore and, after a separator in the danger colour, "Delete for good…" (asked first; no bare red × — the app's dismiss glyph — that deletes); the header's "Empty trash" is a secondary button with danger text, only the confirmations are solid red, and both say "This cannot be undone." | — |
| `cleanup` | `CleanupView` — notes and attachments nothing refers to ("Cleaning up" below) | `kind?`, `selected?` (written when a row opens a file, so Back returns to the same choice) |
| `settings` | `SettingsView` — one section at a time: on wide windows the list of sections on the left, choosing one replaces the route's section (no history entry); on narrow ones (under 900 px, phones) a route without a section shows the **list of sections** (`settingsHome`), drawn as the page's menu sheet (`BMenu.sheet`'s `BMenuItem`s: no cards, no coloured tiles): the Notebook row first (`settingsHomeRow-vault`, the notebook's name muted beside it), then the sections in groups set apart by a thin line, without headings — Appearance, Editor | Reminders, Search, Extensions | Sync, Backups, Privacy | General, Keyboard shortcuts (not on touch UIs), Automation, Application — each a row with its line icon in the muted colour, its title and an arrow (`settingsHomeRow-<key>`); a row opens its section as a step of the history (Back returns to the list), a route with a section shows it at once. General, Appearance (`appearance`), Editor, Notebook (also this device's "Follow links to folders outside the notebook", `AppSettings.followsOutsideLinks`, default off, never in `.brulion/config.json` — a sync peer must not switch it on —, pushed into `Workspace.setFollowsOutsideLinks` → `Vault::setFollowsOutsideLinks`, and read by brulion-cli and the background check: a toggle rescans, the linked files come or go as changes; off, a file opened from such a folder is offered as a copy, and neither its pictures nor its PDFs are shown. Devices that followed such links before it was a setting keep them: while the setting was never chosen, each notebook's first open checks once (`AppSettings::checkOutsideLinksOnce`, "vaults/outsideLinksChecked/<id>") and switches it on — before the index and sync see the notebook — only when this device knew the notebook before (state in its device folder, or a recent notebook) and it has such a link; a new device, or a link planted after the check, never does), Reminders, Sync, Backups (`backups`, "Backups" below), Automation (`automation`: brulion:// links and the notebook's key, "Automation (brulion:// links)" above), Application (on Android and Windows "Check for new versions" — `AppController.updates.enabled`, "Window layout"; on the desktop "Keep running in the system tray" and "Click on the tray icon" Window | Calendar — `AppSettings.trayClick`, the tray calendar above, disabled without a tray; on Windows "Start when you sign in" — `AppController.startAtLogin`, the Run key, shown disabled with a separate `BRULION_PROFILE`, docs/WINDOWS.md "Autostart"; on the desktop "Browser extension (Web Clipper)": each browser's row with its state — Connected (the user's or the system's host manifest naming a program that is there), Not connected, Not installed for you, a broken one with the program it misses — and "Connect browsers" / "Connect again", "Disconnect" (only the user's own manifests), "How to install the extension" — `WebClipperSetup` (`src/app/shell/webclippersetup.h`, the same code as `brulion-cli clip install`: `clipper/hostinstall.h`), docs/CLIPPER.md; on the desktop with "For developers": the switch that lets AI agents drive the running app, docs/CLI.md "Remote control"). Reminders → sounds: on Windows the "Ringtone" menu lists Windows' notification sounds (Notification, Alarm 1–10, Call 1–10…) and "Sound file…" is disabled. Each group is a heading, a note and its rows divided by hairlines: in a card from 600 px up; below 600 px the card is flat (`BCard.flat`: no surface, border or padding), so heading, note and rows share the page's 16-px edge and the headings and the 32-px gap set the groups apart (the sync section's status rows too). The custom accent's empty "+" swatch is `bgElevated2` in the dark (`bgSunken` in the light) | `section?` |
| `logs` | `LogsView` — "Event log" (see below) | `group?`, `level?`, `fromMs?`, `toMs?`, `run?` |
| `chat` | `ChatView` — the AI chat ("AI chat" above): the conversations, one conversation, a new one | `path?`, `new?`, `page?`, `prompt?` |
| `drawing` | `DrawingView` (`qml/drawing`) — the drawing editor on a drawing file ("Drawing editor" below); title: the file's name without `.svg`, `.excalidraw`, `.excalidraw.md` | `path`, `open?` (screenshots: `style`, `more`, `paper`, `theme`, `shape`, `select`, `backdrop` — the page picker of the backdrop's PDF) |

**The start screen** (`OnboardingView`) is shown before a notebook is open: the first run (after
the intro), a start whose notebook is gone, "Open another notebook…". Its job is one choice, made
quickly by someone coming back and explained to someone new:

* **Recent notebooks** (`AppController.recentNotebooks`, the C++ model `RecentNotebooks` over
  `AppSettings.recentVaults`, at most 12) come first when there are any. A row: a folder icon in
  a rounded square, the name (`label`), on its right when it was last opened (`textSm`, muted:
  "Today 08:12", "Yesterday 17:40", "Mon, Nov 16", "Oct 17, 2025" — in Polish "Dziś 08:12",
  "Wczoraj 17:40", "Pn 16 lis", "17 paź 2025"; the app's clock, so `--now` too), under it the
  folder (`textSm`, muted, elided in the middle, the home folder as "~" on Linux; the whole path in
  a tooltip when elided). A click / tap or Enter opens it (`AppController.openRecentVault`). Every
  row can leave the list ("Remove from list": the row's ×, shown on hover and keyboard focus with a
  mouse, always on touch; Delete on a focused row) — the folder itself is never touched.
  The folders are checked off the GUI thread whenever the screen shows and when the window comes
  back to the front (a drive plugged in meanwhile): `RecentNotebooks` gives each its
  `availability` — "available", "missing" (nothing there, or a file), "unreachable" (no answer
  within 4 s: a network share that hangs; or a folder it may not read); "" / "checking" until then,
  shown as available. A missing or unreachable notebook is marked: a crossed folder (`folder-x`)
  on amber (`warningBg`, `warningText`), its name muted, "Folder not found" / "Not available"
  ("Brak folderu" / "Niedostępny") in place of the time, and a "Remove from list" button (phones:
  the row's ×, whose name it is). Opening one anyway warns and creates nothing (it stays marked: a
  drive plugged in again brings it back). A notebook in the system's temporary folder
  (`QDir::tempPath()`, `/tmp`, `/var/tmp`: a test run's, an archive's) whose folder is gone never
  comes back — it leaves the list instead (at the start, `RecentNotebooks::dropVanishedTemporary`,
  and whenever a check finds one). In the browser every notebook is available (IndexedDB), on
  Android a folder on shared storage without "All files access" too (opening it asks).
* **Open or create a notebook** — the start card with the one primary action: its heading, what to
  choose (desktop: a new folder, one synced with Syncthing, or an Obsidian vault, which opens as it
  is; Android: shared storage and "All files access", or the app's storage; browser: the one
  notebook in its storage), the folder field with **Browse…** (desktop: the system's dialog,
  `FilePicker`; on phones an icon button), the waiting card for "All files access" (Android), and
  **Open notebook** as a large primary button across the card (the browser: **Start with an empty
  notebook**; Android adds **Use app storage**). The field starts with the current notebook, else
  the suggested folder; a missing folder is created. The app for Google Play (`AppController.
  playStore`, no "All files access") has no field: **Choose a folder…** is its primary button — the
  system's folder dialog, then `AppController.openDeviceFolder()`: a notebook in the app's storage
  kept in sync with the picked folder, its notes copied in first while the splash says how far
  ("Copying the notes from the folder “Notatki”… 12 of 340", `AppController.loadingText`) —
  and **Use app storage** below it (docs/ANDROID.md "A folder picked in the system's dialog").
* **Other ways to start** — one card of three rows (an icon in a rounded square, the title, one
  line about it, a chevron): **Import notes** (the chosen notebook opens on its import tab),
  **Open from GitHub…** (`GitHubOpenDialog`) and **Try the sample notebook** ("Sample notebook" —
  "Przykładowy brulion" with the Polish UI: its notes are in the UI language).
* **What Brulion is** — only for a first run (no recent notebook): two highlights (plain files,
  journal and reminders) and "How do notes get to my other devices?" (Syncthing, Nextcloud), in a
  sunken card; text at the UI's base size, muted.

Layouts: from 880 px of width two columns that fit the window — at 1280×800 and 1920×1080 nothing
scrolls (`tst_shell_onboarding_qml_desktop` checks it): the logo (64 px) and "Welcome to Brulion"
(`text3xl`; the tagline under it for a first run only) over the columns, the block at most 1120 px
wide and centred; someone coming back has the recent notebooks on the left (56 %; a long list
scrolls inside its card, its scroll bar always shown and the last row fading into the card's
edge) and the start card with "Other ways to start" on the right; a first run has the start card
on the left and "What Brulion is" on the right. Narrower (tablets, phones, a small window) one
column at most 640 px wide that scrolls, in the same order — recent notebooks (the first five,
then "Show all (12)"), the start card, other ways, what Brulion is. Phones: 16-px edges, the logo
48 px, `text2xl`.

Keyboard: the focus starts on the most recent notebook (a first run: on **Open notebook**); the
list is one tab stop (Tab comes back to the row focused last), Up and Down move along it (it
scrolls to the focused row), Enter opens, Delete removes from the list and hands the focus to the
next row; Tab goes on to the field, Browse…, Open notebook and the other ways (the rows' × and
"Remove from list" buttons are for the pointer, not tab stops). Accessibility: the
headings are headings, the list a list ("Recent notebooks"), a row says its name, then why it
cannot open, its folder and "Last opened: …"; the remove buttons are named "Remove from list"
with the notebook's name as their description. `tst_shell_onboarding_qml` (phone, Polish) and
`_desktop` (English) drive it with real input: a missing notebook marked and removed, a vanished
temporary one dropped, "Show all", the two columns without scrolling, the one primary button
creating and opening a typed folder, the keyboard on the list.

Test runs never reach the user's own recent notebooks: ctest's GUI tests and the screenshot
scripts run in private homes (XDG folders; on Windows `BRULION_PROFILE=test-<name>`), and a run
that only tests or photographs the app (`--selftest-*`, `--journey`, `--screenshot`) without a
home of its own uses the profile "test" by itself (`--profile <name>` / `BRULION_PROFILE` choose
another; src/ipc/profile.h, docs/CLI.md "Profiles").

**The intro** (`IntroView`, `qml/views`): on the first start, the status "onboarding" shows four
steps before `OnboardingView` (`AppController.introPending`: `AppSettings.introDone` was never
set on this device — a device that opened a notebook before, an update, never sees it —, or
`--intro`; `--onboarding` skips it). Each step changes its settings at once, all of them stay in
Settings:

1. **Language** — English (the default) or Polski (Polish) as two cards named in their own language; the
   UI switches at once (`AppSettings.language`).
2. **Look** — the theme as four previews drawn in their own colours (`ThemePalette.colors`):
   Black (the default: dark with the black background), Dark (blue-grey), Light, System; the
   accent presets, the text size, reduce motion.
3. **Permissions** — what reminders need here, each a card with its state (Allowed / Not allowed /
   Optional) and an Allow button (`AppController.requestPermission`): on Android notifications,
   exact alarms, alarms over the lock screen and, optional, "All files access" (only for a folder
   on the phone's storage); in the browser its notifications; on the desktop nothing to allow — the
   tray and (Windows) start at sign-in instead. The reminders switch and a test notification
   everywhere.
4. **Notes** — the settings of the first notebook: journal mode (Outline / Document cards),
   new pages' mode, journal file names (`yyyy-MM-dd`, `dd.MM.yyyy`, Logseq's `yyyy_MM_dd`, shown
   as today's name), first day of the week, default reminder time (the clock dialog). Kept in
   `AppSettings.newNotebookConfig` and written to `.brulion/config.json` of the next notebook
   opened when it is new — no notes, no config.json, no Obsidian settings
   (`AppController::applyIntroNotebookConfig`); dropped after the first notebook of the user's
   either way (the sample notebook and the documentation do not count).

A progress row (the steps as dots, "Step 2 of 4", **Skip**: the defaults) above, **Back** and
**Continue** (the last step: **Choose a notebook**, `AppController.finishIntro()`) in a footer above
the keyboard and the gesture bar. Back also with Android's back gesture (`Main.qml` asks
`IntroView.backStep()` first) and Alt+←, Continue with Ctrl+Enter. Settings → General → **Run
the intro again** (`AppController.restartIntro()`) closes the notebook and starts it again.
The journey (`--journey`) walks it: Polish, the run's theme, a journal mode clicked
(`Journey::intro`, screenshots `intro-*`).

## Automation (brulion:// links)

Other apps, launchers, scripts, bookmarks and automation tools (Tasker, Automate, MacroDroid,
KDE's and GNOME's shortcuts, a browser's bookmark) reach Brulion through **brulion:// addresses**
(Obsidian's URI scheme, Logseq's `logseq://`, x-callback-url): open a page, a heading or a block,
a day; add a note, a task with a reminder, a new page; search; open a view; run an extension's
command. The core is `src/core/automation` (`uri.h` reads and writes addresses, `policy.h` decides
what one may do, `edits.h` the pure edits, `plan.h` works a request out for a notebook and carries
it out); the app's part is `src/app/shell/appautomation.cpp` (AppController::receiveUri), the
command line's `uri build | parse | run | open` (docs/CLI.md "brulion:// addresses"), Android's
docs/ANDROID.md "Automation", the browser's docs/WEB.md "brulion:// links".

### The address

```
brulion://<action>[/<page path>]?<parameter>=<value>&…
```

* `brulion:` or `web+brulion:` (the browser's protocol handler), `//` optional, any letter case;
  `brulion://x-callback-url/<action>?…` too. A path after the action is the page
  (`brulion://open/Projekty/Dom`; for `new` the title).
* The query is read as a form: `%XX` are UTF-8 bytes and **`+` is a space** — a plus sign is
  `%2B` (every URL encoder writes it so); Polish letters may also be typed as they are. `#` is
  part of a value: `page=Dom#Zakupy` is the heading "Zakupy" of the page Dom, `page=Dom#^abc` its
  block, as in `[[links]]`.
* Parameter names ignore letter case; a parameter given twice, a `%` without two hex digits,
  invalid UTF-8, an unknown action or **a parameter the action does not know** (a typo:
  `heding=`) make the address invalid — nothing is guessed. Control characters are dropped from
  values (line breaks and tabs stay; `\r\n` becomes `\n`).
* Limits: the address at most 100 000 characters, a value at most 50 000 once decoded, 32
  parameters.

| Action | Parameters | What it does |
|---|---|---|
| `open` | `page` (also `file`, `path`: a path with or without `.md`, a page name or alias as `[[links]]` read it, `today`, a date, a date phrase — "wczoraj", "w piątek" — when no page has that name), `heading`, `block` (`abc`, `^abc`, `((abc))`), `line` (from 1) | shows the page there; a day without a note shows its empty day (never created by opening it); a page that does not exist is an error |
| `daily` (`journal`, `today`) | `date` (ISO, `today`, "wczoraj", "next friday"; default today) | shows the day |
| `new` (`create`) | `title` (also `name`, `page`; "Projekty/Dom" makes the folder), `folder`, `template` (a page of the templates folder by name or path: docs/TEMPLATES.md), `text` (also `content`: after the template's text), `mode` (`outline`, `document`), `open` (default yes) | a new page — **never over an existing one** (also one differing only in letter case): that page opens instead, error `exists` |
| `append` / `prepend` | `text` (also `content`), `page` or `date` (default today's journal, made from the journal template when missing), `heading` (the end — `prepend`: the start — of its section; `Dziś`, `## Dziś`, `Plan#Dziś`), `block` (its last / first child), `create` (yes: a missing page is made), `open` | adds the text as one block (`- ` unless it starts with a list marker; further lines its continuation): at the page's end, or at its start — after the frontmatter, the page's properties and a title heading |
| `task` | `text`, `due`, `scheduled`, `start` (date phrases), `remind` (a date and time phrase; a date alone gets the notebook's default reminder time), `alarm` (yes: the reminder rings as an alarm; or the alarm's own phrase), `every` (a recurrence: "co tydzień", "every weekday"), `priority` (highest … lowest, najwyższy … najniższy), `parse` (no: a date in the text stays text), the target as `append` | `- [ ] text 📅 … ⏰ …`; without date parameters a date phrase in the text is taken out as quick capture reads it ("jutro o 18" a reminder, "w piątek" a due date) |
| `capture` | `text`, `open` | quick capture into today's journal (a date makes a task); without text the quick capture dialog opens |
| `search` | `query` (also `q`, `text`) | the search view with the query |
| `view` | `name` (journals, calendar, tasks, habits, graph, search, allPages / pages, tags, sync, importExport, trash, cleanup, settings, logs, chat) and its parameters: `date` (journals, calendar), `mode` (calendar), `query` (tasks, search), `folder` (search, allPages), `tab` (sync, importExport), `section` (settings) | shows the view |
| `run` (`command`) | `extension`, `command` (or `extension=silownia/rekord`), `args` (a JSON object: the command's `ctx.args`), `page` (its `ctx` page) | runs the command of an extension **approved on this device** (docs/EXTENSIONS.md); an extension waiting for approval, off or without that command is an error (`not_approved`) — an address never approves code |
| `pair` | `url` (also `address`: a computer that runs `brulion-cli acp serve`), `code` (its `acp pair` code) | Settings → Extensions → AI agents with the pairing dialog filled in ("AI agents (ACP)"); the user pairs — a link never pairs by itself. `acp pair` shows it as a QR code the phone's camera opens |
| `chat` | `prompt` (at most 8 000 characters once what nobody sees is taken out; the rest left out), `partner` (who answers: `acp:<id>`, an extension's `id/command`, or the name the chat shows — only one set up on this device, else the default with a note), `page` (the page it is about, when the notebook has it) | a **prompt link**: a new conversation of the AI chat with the prompt in its field — **never sent**, the user reads it and presses Send ("AI chat": `fromLink`, `draft`). Zero-width characters, bidirectional marks and controls, Unicode tag characters, variation selectors that hide bytes and every other format character are taken out first (core `automation/chatlink.h`: `cleanPrompt()`; an emoji's own joiners and presentation selector stay), and the view warns that they can hide instructions. Made by "Copy prompt link" and `brulion-cli uri build chat` |

Common parameters: `vault` (also `notebook`: a notebook of this device by its name or its folder's,
or its absolute folder; another one than the open is opened first; a name several notebooks share
is an error), `key` (below), `x-source` (the caller's name, shown in the confirmation as what the
link says), `id` (echoed in results), `x-success` / `x-error` / `x-cancel` (callbacks, below).

### Safety

Any web page, any app and any program of the device can fire an address, so (`policy.h`):

* **Opening** — `open`, `daily`, `search`, `view`, `capture` without text, `pair`, `chat` — **runs at
  once**; it changes nothing (a prompt link only fills the chat's field).
* **Writing** (`new`, `append`, `prepend`, `task`, `capture` with text) and **running an
  extension's command** need the notebook's **automation key** in the address (`key=…`) — or the
  user's **Write** in the dialog **"Write from a link?"** (`ShellAutomationDialog`), which says where
  it would write (today's journal, the page and its section, a new page, the command) and shows
  exactly what, as plain text in a monospace well, the reminder's time, the notebook, and the
  link's own `x-source` name as "The link says it comes from …", never as a fact. "Cancel" writes
  nothing. The confirmation is asked every time: the caller of a link cannot be known (a browser
  does not say which page fired it), so there is no "always allow" for links without the key.
* The key: **Settings → Automation** (`ShellAutomationSettings`) makes one per notebook and
  device — 32 random characters (160 bits), shown on request, copied, renewed (the old one stops
  working), removed. It lives in `<app data>/secrets/automation-keys.json` (owner-only, next to the
  other secrets), never in the notebook: it does not travel with a sync, and every device has its
  own. Compared in constant time; letter case and spaces around are forgiven. A **wrong key is
  refused** whatever the address asks (the caller hears why).
* The setting **"Links without the key"** (`AppSettings` `automation/links`, this device):
  **Ask** (default: writing asks), **Only open** (writing only with the key, no dialog), **Off**
  (addresses do nothing, not even with the key).
* **Never overwritten, never deleted**: writes add one block (compare-and-swap through the vault,
  History keeps what it replaced; a note that is not UTF-8 is left alone) or create a page whose
  name is free. Writing through the vault and not the editor's save path, a block added to an
  extension page never approves its code — changed code waits for approval as code from a sync.
* **Callbacks** (`x-success`, `x-error`: opened with `path`, `line`, `id`, or `errorCode` and
  `errorMessage`, added to their query) are used only for a request with the **valid key**: without
  it a web page could learn from them which pages exist. Never Brulion's own scheme, `file:`,
  `data:`, `javascript:` (a loop or a local file). `x-cancel` is read but never used: a request with
  the key is never asked about.
* The event log records an address's action and where it came from, never its text or key
  (`redactedUri()` masks a key wherever an address is shown).

The confirmation, an error ("No page “Zakupy” in this notebook.", "The Brulion link has a part
Brulion does not know: heding") or the outcome ("Task added to today's journal" with an **Open**
button; a new page and `open=yes` show the note) are toasts. Addresses that come while a notebook
opens wait for it (at most 8); one for the documentation goes to the user's notebook (as quick
capture); without any notebook the app asks for one first and runs it then.

**Errors** (`plan.h`, the same codes for the command line, Android's result and the callbacks):
`unknown_parameter`, `missing_parameter`, `missing_text`, `missing_title`, `page_not_found`,
`not_a_page`, `heading_not_found`, `block_not_found`, `exists`, `template_not_found`,
`date_not_understood`, `recurrence_not_understood`, `bad_priority`, `bad_args`, `unknown_view`,
`not_utf8`, `conflict`, `failed`; the address's own (`too_long`, `no_action`, `unknown_action`,
`bad_encoding`, `duplicate`); the policy's (`links_off`, `key_required`, `wrong_key`);
`unknown_notebook`, `ambiguous_notebook`, `not_approved`, `needs_app` (the command line and
Android's background run: opening and commands need the window).

### Where addresses come from

| From | How |
|---|---|
| Linux | the desktop entry declares `MimeType=x-scheme-handler/brulion;` and `Exec=brulion --open %U` (packaging/, the Nix package, the AppImage's desktop entry): `xdg-open 'brulion://daily'`, a link in a browser; a running Brulion gets it from the second launch (`uri` message, ipc/appmessage.h) |
| Windows | the installer registers the URL protocol `brulion` (`HKCU\Software\Classes\brulion`, docs/WINDOWS.md): `start brulion://daily`, a link |
| Android | links (VIEW), Tasker's intents, Quick Settings tiles, launcher shortcuts (docs/ANDROID.md "Automation") |
| Browser | `web+brulion:` through `navigator.registerProtocolHandler` (Settings → Automation: **Allow**) and the installed app's `protocol_handlers` (docs/WEB.md "brulion:// links") |
| A note | a `[link](brulion://…)` clicked in a note (Workspace::openUrl → `automationUriRequested`) — checked like any other: notes come from anywhere |
| Command line | `brulion 'brulion://…'`, `brulion --uri <address>`, `brulion-cli uri open` (to the running app), `brulion-cli uri run` (headless, docs/CLI.md) |

Settings → Automation also builds links: Quick note / Task / Add to a page / Open / New page,
the page, heading, text and reminder fields, "With the key", the address as it comes out (monospace,
wrapping anywhere), **Copy link** and **Try it** (the app receives it as any link: a write without
the key asks). `AppController.automationLink()` / `pageAutomationLink()` write them (`vault` is the
notebook's folder name, so a link works whichever notebook is open).

## Tabs, split view and windows

Several pages open at once (Obsidian's tabs, panes and pop-out windows), kept simple: one tab bar
for the main window, at most two panes side by side, and pages in windows of their own on desktops.
The model is `TabSet` (`src/app/shell/tabs.h`, pure, `tst_shell_tabs`); `AppController` keeps one
for the open notebook (`tst_shell_navigation`), the shell shows it (`ShellTabBar`, `ShellTabPane`,
`ShellPageWindow`; self-test `checkTabs`).

* **A tab** has its own back / forward history (`RouteHistory`) and the view state of its current
  route: where its page or the journal stream was scrolled (`PageView.viewState()` — the first row
  in view and how far into it —, `JournalsView`'s — the day at the top). The pane hands that state
  to the tab before it shows another one and gives it back when the tab is shown again (the view
  is then at that place, the route's `line` / `blockId` is not flashed again); a new route in the
  tab (a link, Back) drops it. `AppController.currentRoute`, `canGoBack`, `canGoForward`, `atHome`
  are the main window's **focused tab**'s (`focusedTab`); `navigate()`, `replaceRoute()`, `back()`,
  `forward()` act on the **active tab** (`activeTab`) — the focused tab, or a page's own window
  while it has the focus (its views navigate in it; `Main.qml` gives the main window its tab back
  when it is activated: `mainWindowActivated()`). A pane follows its tab through
  `tabRoute(id)` / `tabRouteChanged(id)`.
* **Opening a tab:** Ctrl+T or the bar's **+** (a new tab at home), a **middle click** or a
  **Ctrl+click** on a page in the sidebar (tree, Favorites, Recent), All pages, search results,
  linked references and a link preview's header or links; a **middle click** on a page, tag or
  block link in the editor (Ctrl+click there picks blocks: "Selecting and copying"); the link menu's
  and the sidebar's / All pages' row menus' **Open in new tab**; the top bar's More and the
  palette's **Open in new tab** (a copy of the view shown) and **New tab**. A new tab goes
  right after the focused one, is shown and has no history (Back does nothing there; its first
  route is the page). These go through `OpenIn` (`qml/components/OpenIn.qml`): the shell sets
  `OpenIn.tabs`, a click brackets its own navigation (the editor's `activateLink`, a row's
  `openPage`) with `OpenIn.begin(target)` … `OpenIn.end()`, which the shell hands to
  `AppController.beginOpenIn(target)` / `endOpenIn()`: its one navigation goes to a new tab
  (`target` "tab") or window ("window") — the editor and the lists need no shell (without it, in the
  editor's tests, only the usual click). At most `TabSet::MaxTabs` (100) tabs: more says so.
* **The tab bar** (`ShellTabBar`, wide windows): between the top bar and the view, shown while
  there are two tabs or more (or the view is split); 38 px high (44 on touch UIs). Square tabs
  side by side with full-height hairlines between them (IDE-like), equal widths (120–220 px; past
  that the bar scrolls: with the wheel, or a whole tab at a time with the arrows ‹ › shown at its
  ends — the tab cut by the edge comes in whole, or the next one —, and the focused tab glides into
  view whenever it changes: a new tab at the end, Ctrl+1…9, a narrower window), the view's icon and title (`ShellRouteTitles.routeIcon` —
  the command palette's icon of the view: the trash's bin, the sync center's, the event log's,
  a page's only for a page — and `routeTitle`, "Event log" for the log; a daily note's date and a
  journal day's in the language shown, following a switch), ×
  (shown on the focused tab and on hover). The tab of the focused pane is drawn like the view below
  it with a 2-px accent edge along its top (`shellTabAccent`), the other pane's tab of a split
  view like the view without the edge. On the layered dark backgrounds (`Theme.layered`: Graphite,
  Ink) the tabs are a browser's: no hairlines, no line under the bar; the shown tab
  (`shellTabShape`) starts 5 px below the bar's top with rounded top corners (`Theme.radius`) and
  flares into the view with concave corners of the same radius (the view's colour in a square
  beside each bottom corner less a quarter circle of the bar's colour), its accent edge between
  the corners; a tab under the pointer gets a rounded well. A
  click shows a tab (in the focused pane; a tab already shown in the other pane focuses that pane),
  a middle click or × closes it, a drag along the bar moves it (`moveTab`), a right-click or long
  press opens its menu: Close tab, Close other tabs, Show in split view / Close split view,
  Move to new window (desktop), then the extensions' items (`tabMenu`, "Extension items in the
  UI"). Closing the shown tab shows its neighbour (the next one, else
  the previous one); closing the last one leaves a fresh tab at home.
* **Split view** (wide windows): the main area holds two panes side by side (`SplitView` with
  `BSplitHandle`): **Split view** (the top bar's More, the palette, a tab's menu Show in
  split view — that tab beside the main one) opens a copy of the focused tab in the right pane,
  focused. A click (any button) or the keyboard focus in a pane focuses it; the top bar, the side
  panel's context, the palette's page commands, the window's shortcuts (Ctrl+E, Ctrl+F, Ctrl+;)
  and the bar's accent line follow the focused pane (the other pane's views get `paneActive:
  false`: their window-wide shortcuts are off — two would be ambiguous —, a PDF there writes no
  page into the route). Closing the split pane's tab, or **Close split view**, leaves one pane (its
  tab stays in the bar unless closed). Only vertical splits, two panes: the side panel stays the
  place for a third page. A narrow window shows only the focused tab.
* **A page in its own window** (desktop only; Android and the browser have one window):
  **Open in new window** (the link menu, the sidebar's row menu, the top bar's More, the
  palette) or a tab's **Move to new window** (`detachTab`). `ShellPageWindow` — an
  `ApplicationWindow` without a transient parent, 760 × 720 — has its own bar: back / forward (the
  tab's history), the title, **Move to main window** (`attachTab`: a tab of the bar again);
  Alt+← / Alt+→, Ctrl+W (closes it) and its view's own shortcuts work in it. It shares the
  Workspace: its editor and the main window's are two editors of the same notes, and each one's
  saves reach the other (`EditorHost::fileChanged`), as with the side panel. Toasts and the shell's
  dialogs (rename, move, delete) stay in the main window. Closing the window drops its tab.
* **Remembered per notebook** on this device: `AppSettings` key `tabs/<Vault::idForPath>` — the
  bar's tabs (windows' tabs come back as tabs of the bar), each with up to 20 back and 20 forward
  routes and its view state, the main and the split pane, the focused one (`TabSet::save()`,
  JSON). Saved half a second after a change, when the notebook is left (another opens, it closes),
  on quitting and when the app goes to the background; restored when the notebook opens — routes
  of pages deleted meanwhile drop out, malformed JSON means one tab at home. The documentation
  notebook keeps its tabs too (it still opens on its table of contents). `--route` (screenshots,
  tests) starts with one tab showing that route and leaves the saved tabs alone.
* **Phones and narrow windows:** no tab bar and no split; the tabs are kept (a wide window may
  have opened them; the link menu's Open in new tab opens more) and **collapse into a tab
  switcher**: with two tabs or more the top bar shows a small square with their number before
  Search (`topBarTabs`); it opens a menu of the tabs (titled as in the tab bar; the shown one
  checked as the notebook switcher checks the notebook open; a tap shows a tab), New tab, Close
  tab (with their keys' hints on a narrow desktop window), Close other tabs. The Android back key
  walks the shown tab's history.
* **Performance:** only the tabs shown — at most the two panes and the windows — have views (an
  editor, a PDF viewer…); every other tab is a route, its history and a view state. A pane that
  shows another tab of the same view keeps its view (as a new route does) and switches the page;
  of another view the view is made again ("docs/PERFORMANCE.md" "Tabs").

## Exporting and printing a page (`qml/content/ContentExportDialog.qml`)

The page's menu has **Export page…** and **Print…** (Ctrl+P; also the palette's "Export
page…" / "Print page…", uiActions `exportPage` / `printPage`, handled by the PageView on
screen; a picture, a PDF or another file has no "Print…"). Both open `ContentExportDialog` —
"Print…" starts printing right away — without going through Import and export:

* **Format** tiles (three columns, two on phones): Markdown (the page's file as it is,
  `ExportController` format `markdown-file`, page scope only), Markdown with attachments
  (`markdown-zip`: the page and the pictures and files it links to), HTML (`html-single`, the
  pictures inside), PDF, Word (DOCX) and Pandoc Markdown; the chosen one explained below.
  **Pictures from the internet** (Keep addresses / Include copies, `AppSettings.exportWebPictures`) for the
  HTML, PDF, Word and Pandoc formats.
* **Protect with a password** (`ExportPasswordBox`, the HTML format only:
  `ExportController.protectApply` / `protect` / `setPassword()`): the password typed twice (the
  buttons wait until both match), never stored — the dialog forgets it when it closes. The file is
  the lock page of password-protected publishing (core `publish/protect.h`, "Publishing" →
  "Password-protected pages"): the document sealed with AES-256-GCM under K = PBKDF2-HMAC-SHA256(the
  password, a fresh random salt every export, 600 000 rounds), the generic title "Protected page",
  the password form; the browser opens it with WebCrypto — also from a disk or an e-mail
  attachment (`file:` is a secure context in Chromium and Firefox); the pictures are inside the
  sealed document. **Copy to clipboard** is hidden then (it would copy the plain page); printing is
  never protected. Also in Import and export (`ExportPanel`) for the "HTML document" of any scope.
* **Save as file…** (the browser: **Download**) makes the file in the background (a progress
  bar and **Cancel** under the tiles), then the system's save dialog (`FilePicker`, Android: the
  system's "Save as") or the browser's save picker / a download; **Save to Downloads** on
  Android (MediaStore); **Copy to clipboard** for Markdown and HTML (`PageTools.copyExport`);
  **More export options…** opens Import and export with the page and the format chosen.
* **Print…** (`ExportController.print()`): desktop — the system's print dialog (`QPrintDialog`,
  Qt Print Support, linked on the desktop only), the pages painted on the printer
  (`io::printNotes`: the PDF's look, its page size and margins from the dialog, a page number
  at the bottom); Android — the PDF export handed to the system's print service
  (`AndroidBridge::printPdf`: PrintManager with a WebView-free PrintDocumentAdapter copying the
  file, `PdfPrint.kt`; its printers and "Save as PDF"); the browser — the HTML document in
  its print dialog (a hidden frame sandboxed without scripts, `platform/wasm/wasmopen.h`).

PDF and DOCX are Brulion's own (`io/paper.h`, no Pandoc or LaTeX, also on the phone): the HTML
document export laid out for paper — headings, lists with the tasks' checkboxes, tables with
borders (a table above 1500 rows or 10 000 cells keeps its first rows), code, math as the HTML
shows it, pictures scaled to the page, Mermaid diagrams (and other SVG) as pictures, dates and
reminders in words ("due: 2026-11-20"; colour emoji fonts do not print); links to the web stay
links, links inside the notebook are text. The PDF embeds its fonts (Polish letters as written);
the paper is A4, Letter where the system's locale uses it (`io::paperSizeFor`). The DOCX has
Word's styles (heading 1…6, Source Code, Quote, Hyperlink), real list numbering, hyperlinks,
tables with header rows (at most 63 columns, Word's limit) and the pictures as embedded media.
Tests: `tst_io_paper` (also through LibreOffice when installed), `tst_appio_pageexport` (printing
into a PDF read back), the import self-test's step 8 (the dialog, the save dialog's name, printing).

## Publishing (`qml/io/PublishPanel.qml`, `src/app/io/publishcontroller.h`, core `publish`)

A page, folders, tags or the whole notebook as a website, with one click: the **Publish** tab of
Import and export (`PublishPanel`; also "Publish…" in a page's menu, "Publish folder…" in a
folder's menu in the sidebar, "Publish…" on a tag's overview, the palette's "Publish…" and "Publish
page…" — `{view: "importExport", tab: "publish", add: "page" | "folder" | "tag" | "notebook", path,
folder, tag}`: the item is added to the chosen target and previewed; with no target yet the new
target's dialog comes first and the item follows it). The same targets and the same rules in
`brulion-cli publish …` (docs/CLI.md "Publishing").

* **Targets** (`PublishTargetDialog`): where the site goes —
  * **GitHub Pages**: a repository (`owner/name`, a github.com address is understood), a branch
    (default `gh-pages`: made without history when missing; an empty repository gets its first
    file through the contents API), a folder in it (its root or `docs/`, which Pages serves; another
    folder is published but Pages is not turned on for it). One commit per publish with only the
    changed files (text inline in the tree, pictures as blobs); the branch is only fast-forwarded —
    when someone pushed meanwhile the plan is made again on the new head, once. Pages is turned on
    when it is off (a token without the admin right: the site is published and the user told to turn
    Pages on); a Pages site served from another branch is left alone (said). The address: Pages'
    own (a custom domain too), else `https://<owner>.github.io/<name>/`. The token is the one the
    notebooks from GitHub use (the sync center's GitHub tab; "Add a token…" leads there).
  * **Nextcloud**: the server, the user name and an app password, a folder of the account (never
    its root: a public link to everything is never made); the files go up by WebDAV, then a
    read-only **public link** to the folder through the OCS Share API — optionally with a
    **password** (Nextcloud's own share password; its policy may refuse a short one: its words are
    shown, the files stay published) and an **expiry date**. The link is kept and changed in place
    (its password set again when it changed here — the manifest keeps the password's SHA-256,
    never the password —, its expiry), made again when it was removed on the server, removed when
    "A public link" is turned off or the target unpublished. Nextcloud shows a shared folder as a
    list of files: viewers open a page there or download the folder; "One document" gives one file.
  * **WebDAV server**: a folder's WebDAV address, user name and password, and the address people
    open (optional).
  * **Folder on this device** (desktop only): a folder a web server serves or a sync app takes to
    a server; its hidden files (`.git/`, `.htaccess`) are never looked at.
  Every kind: a name, the site's title (default: the notebook's name), the site's address (a custom
  domain; Nextcloud: the link). Targets are this device's (in the notebook's device state folder,
  `publish/targets.json`, shared with brulion-cli; never in the notebook, so a synced or imported
  notebook cannot send notes anywhere): another device sets up its own — on GitHub its first publish
  is still incremental (the branch's blob ids say what is there already). Passwords live in
  owner-only files next to the Nextcloud app password (`<app data>/secrets/publish-<id>`,
  `…-share`, `…-pages` for protected pages), masked in every log line; removing a target forgets
  them.
* **What is published** — the chosen target's card: its items (a page, a folder with its
  subfolders, a tag with its sub-tags, the whole notebook; "Add…" with the pickers, × removes one)
  and switches: **Also the pages they link to** (one step), **Only pages marked publish: true**
  (Obsidian Publish's way; a page added by itself is published anyway), **Extension pages** and
  **Conversations with AI** (off: left out); the content: **Web pages** (the HTML site export: a
  page per note, the index of journals, pages and tags, tag pages, backlinks, the pictures and
  files the pages link to, a search box — `search.js` and `search-index.js`, loaded when the box is
  first used, diacritics ignored; no server needed) or **One document** (`index.html` with every
  page and the pictures inside). Never published: hidden folders and files, conflict copies,
  encrypted pages and files (`*.mdenc`, `*.age`, `*.gpg`, …; `encrypted: true`), encrypted
  fragments inside a page (cut out: Meld Encrypt's markers, ```` ```encrypted ```` / `secret` /
  `age` / `pgp` blocks, armoured messages — docs/SYNTAX.md §4 "Publishing"), pages marked
  `publish: false`; the templates folder only when a page of it is added by itself. A published
  page shows only published pages: its links to others are plain text, its backlinks and the tag
  pages list published pages, its queries and charts count published pages; macros of extensions
  stay as written (they are computed over the whole notebook). Pictures from the internet keep
  their addresses. Pages have no time in them ("Published with Brulion"): an unchanged page is the
  same bytes, never sent again.
* **Preview** (the dry run, `PublishController.preview()`): the chosen pages read in chunks, the
  site built off the GUI thread, the target looked at — "What a publish would do": the counts
  ("3 new · 1 changed · 2 to remove · 20 unchanged"), pages, attachments and size, every file with
  what happens to it (what changes first, at most 300 rows), every file **left out** with its
  reason ("Marked publish: false", "In a hidden folder", "Encrypted", "An extension's code", "A
  conversation with AI", "A template", "Not marked publish: true"), warnings (an item no longer in
  the notebook, encrypted fragments left out). A target that cannot be reached: the plan from the
  manifest of the last publish, said so. Nothing is written.
* **Publish**: only what changed is sent (WebDAV: `If-None-Match: *` for new files, `If-Match` with
  the ETag the last publish left for changed ones; up to three at a time; removed pages deleted with
  `If-Match`, the folders they emptied removed; ETags a server did not give with a PUT read from a
  listing). **Conflicts**: a file changed at the target since the last publish — or there without
  having been published from here, with other bytes — stops the publish before anything is written
  ("Changed at the target", the files listed); **Replace them** (confirmed) publishes over them. A
  file that changes between the look and the upload is caught by the server (412) and never
  overwritten. The result: "Published", the address with **Open** and **Copy link**, a toast; the
  target's tile says "Published today 18:30" (or what failed). The manifest
  (`publish/<target id>.json`: every file's SHA-256, size and the target's version of it) and the
  last status are kept.
* **Unpublish…** (the card's menu, confirmed): every file the target published is removed (GitHub: in
  one commit; files changed there since stay, listed), a Nextcloud link too; the site's other files
  (a `CNAME`, a server's own) are never touched. **Remove the target…**: forgets it and its passwords
  (what it published stays).
* Errors in words (`publish/messages.h`): a sign-in refused, a token that may not push or is
  missing, not found, offline, the rate limit, the branch that kept moving, Nextcloud's refusal
  quoted, a password missing on this device.
* **Password-protected pages** (the target dialog's **Password-protected pages** switch and the
  pages' password; `publish set --protect on` + `publish secret --pages`; core `publish/protect.h`)
  — for any kind of target, made for GitHub Pages and WebDAV servers, which have no passwords of
  their own: every page and file goes up encrypted and the browser asks for the password.
  * **The scheme**: K = PBKDF2-HMAC-SHA256(the password, the site's salt, 600 000 rounds — OWASP's
    figure for SHA-256, the strength of the encrypted notes' 220 000 SHA-512 rounds), 32 bytes;
    each page and file sealed with AES-256-GCM under its own key HMAC-SHA256(K, its salt) — the
    salt and the IV derived from K, the file's new name and its bytes, so an unchanged page is the
    same bytes and the incremental publish sends it once. The site's salt (16 random bytes) is the
    target's (`protectSalt` in `publish/targets.json`, made when protection is turned on; not
    secret). The password never enters the notebook or the store: it is a secret like the others
    (`<app data>/secrets/publish-<id>-pages`), asked for in the target's dialog — a target without
    it on this device says so on its card ("A password of this target is not on this device") and
    a publish stops with "The pages' password is not on this device" (the command line: exit 5
    `no_page_password`). The crypto is core `crypto`'s (BearSSL), the one of the encrypted notes.
  * **What goes up**: `index.html` stays `index.html`; every other page goes to `p/<name>.html`,
    every picture and file to `f/<name>` (`<name>`: 20 hex digits of HMAC-SHA256(K, its path) —
    nothing of the page's title, folder or tag); `style.css` (the site's own) and `.nojekyll` stay
    as they are. Each page is a **lock page**: the generic title "Protected page" (in the app's
    language), `noindex, nofollow, noarchive`, `no-referrer`, a password field, "Remember it until
    the browser is closed" (on by default) and **Open**; the sealed page in a `<script
    type="application/json">` ({v, kdf, hash, iterations, salt, page, iv, data}, Base64) and an
    inline script — no external resources, no `eval`, no inline handlers. The site's search is
    left out (its index would hold every page's text); there are no OpenGraph tags, sitemap or RSS
    to leak anything. Visible without the password: that the site exists, that it is Brulion's, how
    many files it has and their sizes, when they change.
  * **Exported files**: the HTML document export takes the same lock page (`ExportController`
    `protect`, "Exporting and printing a page"): `protectSite()` of the one document as
    `index.html`, with a new random salt per file and the password only in memory.
  * **In the browser**: the script derives K with WebCrypto (`importKey` + `deriveBits` PBKDF2,
    HMAC `sign`, AES-GCM `decrypt`), parses the page with `DOMParser` (nothing in it runs) and puts
    it in place of the lock page (its real title then), scrolls to the address's `#fragment`, then
    fetches the files its pictures and links name (`data-sealed-src` / `data-sealed-href`) and
    shows them as `blob:` URLs (a link's file downloads with its own name). A wrong password: "Wrong
    password.", nothing shown. With "Remember" K (never the password) is kept in `sessionStorage`
    under the site's salt, so the links between protected pages open at once until the tab is
    closed; a kept K that no longer opens a page (the password changed) is forgotten and the form
    shown. WebCrypto needs a secure context: over plain `http://` (except `localhost`) the lock page
    says it needs https. Pictures need `fetch`: a protected site opened from disk (`file://`, a
    folder target) shows its pages but not their pictures — "One document" has them inside.
  * **Changing it**: another password gives other names and other bytes — the whole site is sent
    again and the old files removed; turning protection on removes the plain files by the next
    publish, but a GitHub branch keeps them in its history (publish to a new branch, or unpublish
    and delete the branch, for a site that was public before). Anyone can download the encrypted
    files and try passwords offline: the dialog asks for a long one.

Tests: `tst_publish_selection`, `tst_publish_github`, `tst_publish_webdav` (core: the in-memory
GitHub with Pages, WebDAV and Nextcloud with the OCS Share API, a folder), `tst_publish_protect`
(protected sites: only ciphertext, no names, opened again by a decryption of its own, wrong
passwords, links and files, the same bytes for unchanged pages), `tests/web/publish.e2e.mjs`
(`scripts/web-test.sh --publish`: a protected site made by brulion-cli opened in Chromium and
Firefox — a wrong password, the right one, a link to another page without asking again, the
picture), `tst_appio_publish`
(the controller), `tst_cli_process` (`publishing`: a folder target end to end), the import self-test's
step 9 (the page's "Publish…" → a new folder target → the dry run → "Publish").

## QR code (`qml/qr/QrCodeDialog.qml`, `src/app/qr`, core `qr`)

A note shown as QR codes that another phone's camera reads as text — the quickest way to hand it
over in the same room: no network, no account and no app on the other side (any camera app or
Google Lens shows the text, with Copy and Share). **QR code…** is in the page's menu "⋯", the top
bar's More (notes only; in the journal stream the day being written in), a block's menu (the block
with its children, or the selection it is part of) and the selection's More (a selection over
several days of the journal stream too); the palette's **Share as QR code…** (`page.qrCode`,
uiAction `qrCode`, handled by the PageView or the journal stream on screen). The dialog is the
editor's (`EditorDialogs.showQrCode`, made when first asked for): the page's text as it is now,
unsaved edits and the source being edited included (`PageController.sourceText`), the selection's
Markdown (`selectionMarkdown()`, `StreamSelection.markdown()`).

* **The code**: black on white whatever the theme, its quiet zone (4 modules) around it, as large as
  the window allows (`QrCodeView`: whole device pixels a module, so none is blurred, also at
  fractional scales). Byte mode with the text's UTF-8 — no ECI header, which some scanners show as
  stray characters —; the smallest version that holds it at error correction L, then as much
  correction as that version allows (a short note gets H). Nayuki's QR Code generator
  (`3rdparty/qrcodegen`, MIT) makes it; `qr::encode` refuses more than 2953 bytes before asking it
  (it throws, and the browser's Qt never catches).
* **What it holds** (`qr::noteText`, a segmented switch kept on the device, `qr/format`):
  **Markdown** — the note as written (a notes app on the other side, Brulion, Obsidian, Logseq,
  gets it whole: links, tasks, properties, block ids), every line break LF, no byte order mark, no
  blank lines around — or **Plain text** — what "Copy as plain text" gives (`blocksPlainText`: no
  frontmatter, properties or block ids, "• " and ☐ ☑ ☒, links by their labels, `((id))` by the
  block's text). **Include the title** (`qr/title`, shown only where it makes a difference): the
  title the view shows (a daily note by its day) first — in Markdown a `# ` heading after the
  frontmatter and the page properties, which have to stay first —; a note that starts with a
  level-1 heading of its own has its title already. A note with no block besides its frontmatter
  and properties has nothing to share.
* **Several codes** (`qr::splitText`): a text over 2953 bytes is cut into as few codes as hold it,
  of about the same size (two of 1500 bytes scan more easily than a full one and a scrap), each
  ending after a line break near its share, else after a space, never inside a character (a
  surrogate pair, a letter with its combining marks, an emoji sequence). "Code 2 of 3" with ‹ ›,
  ← → on the code; scanned in turn and joined, the parts are the text byte for byte. More than 20
  codes (`qr::MaxCodes`, about 59 KB) is too long: the dialog says how many it would take and to
  share a selection instead.

Tests: `tst_qr` (codes read back by a reader written apart from the generator,
`tests/core/qr/qrdecode.h`: format and version information, Reed-Solomon codewords, the byte
segment; the smallest version; the text in both formats, the title after a frontmatter; cutting
Polish letters, emoji and combining marks), `tst_qrview` (QrShare's codes and their walk, the
painted code read back at several sizes), `tst_navigation` (the palette's command), the shell
self-test (`checkQrCode`: the dialog from the uiAction, both formats, a long note walked by → and
"Next code").

## Cleaning up (`qml/views/CleanupView.qml`)

**Clean up** (More menu and the palette, `go.cleanup`) lists the notes and attachments nothing
refers to, `notebook::unreferencedFiles` (`notebook/cleanup.h`), computed from the index after it
settles; it waits for a notebook that is still being indexed, when links are not known yet.

* **What counts as a reference:** a link, an embed or a `((id))` reference from another note (also
  from a daily note, a template or a note that is itself listed), in the text, the frontmatter or a
  property; for a note also a tag naming it (`#Dom`, carried by another note or in a ```query
  filter) and a ```query `ref:` / `page:` filter; for an attachment also its file name in the text
  of any note, as written or percent-encoded, in any letter case. The index does not read
  `<img src="…">` or plugins' own syntax, so the name is what protects such a file; the price is that a
  file named like a linked one in another folder (`old/plan.png` next to `assets/plan.png`) is not
  offered. Plain-text mentions of a note's title do not count. Links inside native SVG drawings and
  Obsidian's `.canvas` files are not read, as the index reads them nowhere: a note reached only from
  one of them is offered. A PDF or picture a drawing lies on is offered when no note links to it: the drawing keeps
  its raster (docs/SYNTAX.md "Backdrop") and only "Another PDF page…" needs the file.
* **Never offered:** daily notes, the templates folder and the journal template, favourites (nobody
  links to them on purpose), hidden files and folders (a sync client's `.sync_….db`, `.gitignore`).
* **Choosing:** nothing is ticked at first. A `BSegmented` shows everything, the notes or the
  attachments; a tap on a row (or its check box) ticks it, "Select all" ticks what is shown; the row's
  button opens the file. A row says what deleting it would take away (open tasks, reminders).
* **Removing:** "Move to trash" takes what is **ticked and shown** (a ticked note stays when
  only attachments are shown) after a confirmation, which counts the files holding open tasks or
  reminders; "Undo" in the toast restores all of them, also after leaving the view
  (`Workspace.deletePages`, `restoreAll`). Nothing is deleted for good here: that is the trash's
  business. The list follows the index, so a picture left without links by a removed note appears next.

Tests: `tst_notebook_cleanup` (each kind of reference, what is never offered), `tst_shell_workspace_wiring`
(`unreferencedFiles`), `--selftest-shell` (`checkCleanup`: ticks with real taps and clicks, Back, the filter, the
confirmation, Undo from another view), `tst_content_qml` (the route without QML warnings).

## Backups (`qml/shell/ShellBackupSettings.qml`)

**Backups** is a section of the settings (`section: "backups"`; the palette: `backup.open`, and
`backup.now` for "Create a backup now"), on `Workspace.backups` (`shell/workspacebackups.h`). User's
guide: the page "Backups" of the documentation notebook.

* **Status:** the last backup's time and size with "Create a backup now"; a problem under it in full
  (`statusText`: red for a failed run, plain for files left out). While something runs: what, a
  progress line and "Stop". A failed scheduled run also gives one warning toast per kind of
  failure per session, and a line in the event log; older backups are never touched.
* **Schedule:** the switch (`backupsEnabled`), Every day / Every week, how many to keep (`keepChoices`:
  3, 5, 7, 14, 30; the browser 1, 2, 3). All per notebook on this device.
* **Folder** (not in the browser): the path, "Choose…" (`FilePicker.OpenFolder`; `chooseFolder`:
  a local URL or an Android document that names a file; refused with a message inside the notebook,
  for a cloud document, for a folder that takes no file), "Use the default", on a desktop "Open".
* **The list:** `backupsList`, newest first, one `BListItem` per backup with icon buttons fitting a
  narrow phone: Restore (`backupRestore`), Download in the browser or Share for Android's private
  folder (`backupExport`), Delete (`backupDelete`, confirmed; never to the trash, it is no note). The subtitle
  says "not from this notebook" (a foreign copy: Restore and Delete ask specifically first,
  `backupsForeignRestoreConfirm`) or "incomplete".
* **Restore:** `prepareRestore` checks the backup off the GUI thread; `restorePlanReady` opens
  `backupsRestoreConfirm` with the counts (replaced, back, to the trash, too large); nothing to do is a
  toast. `confirmRestore` saves open editors, waits for a running sync (phase `sync`), makes the safety
  backup and applies the plan; the outcome is a toast. A restore interrupted by closing the notebook or a kill: a
  toast, and on the next open the status names the safety backup, with "I have dealt with it"
  (`backupsDismissInterrupted`).
* **Restore from a file…** (`backupsRestoreFromFile`, under the list): a Brulion backup that is not on the
  list — another device's, a download, one from a folder the app does not watch. The browser opens its own
  picker (`pickRestoreFile`), elsewhere `backupsFileDialog` (`FilePicker.OpenFile`, ZIP archives) →
  `prepareRestoreFromFile(url)`: a local file is read in place, an Android document that is no file on the
  phone's storage is first copied into the cache (phase "Checking the backup…", Stop drops it). The file must
  carry Brulion's manifest: any other ZIP, a text file, a file cut short are refused with a message of their
  own and nothing changes. The same `restorePlanReady` follows (`source: "file"`, `fileName`, `files`,
  `sizeText`), and `backupsRestoreConfirm` is titled by the file's name, says what the file holds and — when its
  manifest names another notebook (`foreign`) — warns that it may replace this notebook's notes with another's.
  Then the same path as a listed backup: safety backup, plan, History, trash. Files of any size are restored
  (above 4 MiB streamed through a staging file). The picked file is never changed or deleted.
* The documentation notebook has none (`available` false, a note instead; the palette's commands off).

Tests: `tst_backup*`, `tst_shell_workspace_backups` (+ `_inline`: restore from a picked file, from the browser's bytes, a ZIP that is no backup), `--selftest-shell` (`checkBackups`: real
taps through a backup, a restore's confirmation, a delete), `tst_content_qml` (the route).

## Encrypted notes (`qml/editor/EncryptedTextDialog.qml`, `qml/content/ContentEncryptedPage.qml`, `qml/content/ContentEncryptionDialog.qml`)

Text and whole pages under a password, in the format of Obsidian's Meld Encrypt plugin (docs/SYNTAX.md
§11 "Encrypted text"; C++: `Workspace.encryption` — `src/app/shell/workspaceencryption.h` — and
`EncryptedPageHost`, `src/app/shell/encryptedpagehost.h`; core `encryption` and `crypto`).

* **Fragments.** The renderer shows a fragment as a locked chip (🔒 with its hint, else
  "Encrypted"/"Zaszyfrowane"; an `encrypted:<from>-<to>` link); a click
  (`PageController.activateLink` → `{action: "encrypted", from, to, fragment, hint}`) opens
  `EncryptedTextDialog` on it. The dialog first tries the passwords remembered in this session
  (`openFragment(path, fragment, "", true)`: nothing asked when one fits, no error when none
  does), else asks — the hint shown as plain text. Unlocked, its text is in a `BTextArea`
  (`Qt.ImhNoPredictiveText`: no learned words from the keyboard) with **Save** (enabled once the text
  changed, or for an old format: `sealFragment`, the same password, hint and look, written as v3),
  **Copy** (`copySecret`: Android 13+ marks the clip sensitive; elsewhere the `x-kde-passwordManagerHint`
  and Windows' clipboard-history exclusion formats), and under **…** **Change password…** (the
  password twice and the hint) and **Decrypt permanently…** (after a confirmation: the plaintext
  where the fragment was). The block editor writes the result (`PageController.replaceEncryptedFragment`:
  only while the block still holds that fragment — at its range or, typed around meanwhile,
  wherever it is; one undo step). **Encrypt selection…** (the edited text's menu, the palette's
  `encrypt.selection`, uiAction `encryptSelection`: the selection, else the caret) and the slash
  command `/encrypt` ("Encrypted text…", uiAction `encrypt` of PageController) open the dialog in its
  "new" mode: the text, the password twice, a public hint (no 🔐, 💡 or line break) and **Show a
  lock in Obsidian's reading view** (a visible `🔐γ … 🔐`; off by default: a hidden `%%🔐γ … 🔐%%`).
  The dialog closes and empties its fields when the encrypted notes lock (`allLocked`).
* **Pages.** A `.mdenc` (or `.encrypted`) route shows `ContentEncryptedPage` in PageView — the
  page's name, its hint, the password field (a blank page: a new password twice); the remembered
  passwords are tried first. Unlocked (`Workspace.encryption.unlockPage` → `pageUnlocked`), the
  page opens in the block editor through PageView's `EncryptedPageHost`: every page goes through it
  unchanged but an unlocked encrypted one, whose file it reads decrypted with the page's key and
  writes encrypted (the same salt and rounds, a fresh IV: AES-GCM only on each autosave); the
  host is never swapped for `Workspace`, so a route from an encrypted page to another never saves
  one through the other's host. The page menu offers **Encrypt page…** (a Markdown page, not a
  daily note: `ContentEncryptionDialog` — the password twice, a hint, "Delete the page's version
  history"; the editor lets the page go before its file is written over: `converting`),
  **Change password…** ("Delete the page's older versions": they open with the old password),
  **Decrypt permanently…** and **Lock page**; Export, Print, QR code and Version history are not
  offered for an encrypted page (nor the palette's export and QR code). **New encrypted page…**:
  the palette (`encrypt.newPage`), the sidebar's pages menu and a folder's menu
  (`AppController.requestItemAction("newEncryptedPage", folder)`). The sidebar's tree and the quick
  switcher list encrypted pages by their name with the `lock` icon. A page changed elsewhere is
  read again with the key (another salt: with the password); one no longer readable with them
  locks with a toast and is never written over (`pageLocked` with a reason).
* **The session.** Passwords typed are kept per page in wiped memory (`encryption::Keyring`) and
  tried — at most two — on the notebook's other fragments and pages; Settings → Privacy: **Lock
  encrypted notes after** 1 / 5 (default) / 15 / 60 minutes without use (an unlock, a save, typing
  in the dialog or moving in an unlocked page; also checked on every use, as a paused app's timer
  fires late), **Remember passwords between pages** (off: every fragment and page asks), **Lock
  encrypted notes now**; the palette's `encrypt.lockAll`. The app lock, closing or opening a
  notebook lock everything too. A page about to lock saves first (`pageLocking`).
* Tests: `tst_shell_workspace_encryption` (a Meld Encrypt note opened, edited and saved through the
  host and the editor; locks; conversions; fragments; the plaintext looked for in every file of the
  notebook and the device state), `tst_encryption`, `tst_crypto`, the shell's self-test
  (`checkEncryption`).

## App lock (`qml/shell/ShellLockScreen.qml`, `qml/shell/ShellPrivacySettings.qml`)

A PIN — or on Android the phone's own lock — in front of the whole app on this device (C++: the
singleton `AppLock`, `src/app/shell/applockcontroller.h`, over core `applock`).

* **Settings → Privacy** (`ShellPrivacySettings`, its own section of SettingsView): **Set a PIN…** /
  **Change PIN…** / **Turn off** (a dialog: the current PIN, the new one twice — 4 to 16 digits),
  on Android **Use the phone's lock** (Android 10+ with a screen lock: `BiometricPrompt` with
  biometrics or the screen lock's credential; no PIN of Brulion's then) / **Use a PIN instead…**,
  **Lock when Brulion starts**, **Lock after leaving Brulion** (at once, 1 — Android's default —,
  5, 15 — the desktop's default — or 60 minutes in the background: Qt's application state;
  the desktop counts an inactive window too), **Lock after no activity** (the desktop: 5 … 60
  minutes without a key, click, wheel or touch while in front; never by default), on Android
  **Hide Brulion's screen** ("While locked": FLAG_SECURE while the lock screen is up and while Brulion
  is not in front, and, Android 13+, the recent apps' preview left out while the lock is on;
  "Always": FLAG_SECURE while the lock is on — no screenshots or recordings), **Hide the note's text in notifications** (on by default:
  reminders say "Reminder"/"Alarm" and the time — also those the background check arms), on
  Android **Hide notes in widgets** (on by default: the widgets say "Brulion is locked", quick add
  still writes), **Lock now**. All of it is this device's setting (AppSettings `privacy/lock/…`).
* **The lock screen** (`ShellLockScreen`, a modal `T.Popup` at z 3000 over the window's overlay,
  above every popup and the toasts): while `AppLock.locked`, Main.qml hides the stage (nothing is
  drawn, reachable with the keyboard or read by a screen reader) and page windows show "Brulion is
  locked" instead of their page; the tray calendar opens the main window instead. A keypad of
  64 px round keys on touch screens (digits from a keyboard too), a password field on the desktop
  and in the browser; dots count the digits typed. Wrong PINs: after the fifth a wait of 30 s,
  then 1, 2, 5 and 15 minutes (`applock::retryDelayMs`), counted down on the screen and kept across
  restarts (a clock set back before the last failure ends the wait). The PIN is checked off the GUI
  thread (PBKDF2-SHA512, 210 000 rounds; `AppLock.checking`). The phone's lock is asked when the
  screen opens and with its button; a phone whose screen lock was removed meanwhile unlocks with a
  warning (there is nothing left to ask). Back on Android sends the app to the background, never
  past the lock. Locking locks the encrypted notes (`Workspace.encryption.lockAll()`).
* The palette's **Lock Brulion** (`app.lock`) while the lock is on.
* Tests: `tst_applock` (the state machine, waits, the PIN's hash), the shell's self-test
  (`checkAppLock`: a PIN set, the lock after the background, a wrong and a right PIN).

## Find and replace

**In the open note** (`qml/editor/FindBar.qml`, `src/app/editor/notefind.h`). Ctrl+F, the page menu's
"Find and replace…" (so phones reach it), More's "Find in note", the palette's `page.find`, route
`open: "find"`; Ctrl+H opens the replace row as well. In the main pane only: a page in the side panel
has none, the PDF viewer and the graph keep their own Ctrl+F. Enter / Shift+Enter (F3 / Shift+F3)
go through the matches, Esc closes it and gives the keyboard back; Back on Android closes it first.
Its buttons never take focus, so the field keeps the keyboard. Options: "Aa" (case), "ąa" (diacritics),
"Word" (whole word), all off at first (`text/find.h`: "zolw" finds "ŻÓŁW", a no-break space equals a space).

* **What is searched:** the note's actual Markdown (`PageController::text()`; in source mode
  `sourceText`), so markup, link targets, hidden task markers and frontmatter count. Queries are
  literal and one line. `NoteFind` recounts 120 ms after a change (400 ms over 200 KB) and leaves the
  caret alone; only next, previous and the replaces move anything. A new query goes to the nearest
  match from where the bar was opened. An edit leaves no current match ("12 results"): the next one
  counts from its place.
* **Showing a match in block mode:** delegates are virtual and rendered text is not the raw text, so
  the current match is shown in the block's inline editor (`BlockEditor.showFindTarget`): the
  block unfolds, a zoom that does not hold it ends (`PageController.reveal`), the list scrolls, the block
  is edited with the match selected once (`InlineEditor.selectFindMatch`: a later click into the block is an ordinary caret) while `findKeepsKeyboard` keeps
  the keyboard in the bar and the touch toolbar away. A tap in a block ends that. Closing on a touch
  screen leaves the block again, so no keyboard pops up. `PageController::locateText` maps a text
  offset to a row and edit-text range: a file line ends with the same line of the block's body.
* **Limits:** a match in a list marker, a task marker, a fence line or a table (cells have their own
  editors) cannot be selected: its block is selected and flashed, and the replace works all the same.
  Source mode shows every one of them. A match in the frontmatter opens the block's "Edit as text"
  editor rather than a field of the properties panel.
* **All matches:** tinted in rendered blocks (`InlineRenderer::withFindHighlight`: shown text only,
  entities whole, escaped output untouched; such a row becomes a tinted `TextEdit`, see `RichTextPills`) and
  in inline editors and source mode (`MarkdownHighlighter.findQuery`). What is only in the Markdown
  (`**`, a link's target) is not tinted in rendered text, though counted. Text that tables, query
  results and embeds draw themselves is not tinted either.
* **Replacing:** `PageController::replaceTextRanges` applies the change to the whole text through
  `applyText`: one undo step ("Replace all" too, with Undo in its toast), other blocks' bytes
  untouched, the usual debounced save and merge. "Replace" only replaces the match on show: with none,
  or with a recount pending (an edit, an option just changed), it shows one and replaces nothing; the
  same text as the match's moves on. With the replace row open the bar counts the matches inside links
  (`text::isInsideLink`, as the notebook-wide preview) and in the YAML frontmatter: Logseq's leading
  `key::` lines are not counted. In source mode the TextEdit holds the text and its own
  undo: `EditorInput.replaceRanges` is one edit block. `NoteFind` maps text offsets to TextEdit
  positions there, as the TextEdit shows a CRLF as one line break.
* **The journal stream** searches **every day** of the stream (`src/app/editor/streamfind.h`,
  `StreamFind` beside the bar's `NoteFind`): the field says "Find in the journal", the count is over
  all days ("3 of 12 · 4 days"; the current match counted from the top), and every day on screen
  tints its matches. The current match is the bar's `NoteFind`'s, in the day being written in
  (`activeDay`; a tap in another day moves the search there): with `wrap` off, next / previous at
  that day's end — also a new query or a replace with nothing after it there — go on to the nearest
  day with a match (`dayRequested`: `JournalsView` scrolls to it, creating the day, and makes it the
  day written in; `NoteFind.enter()` shows its first match going down, its last going up), round the
  stream; a day alone with matches goes round itself. Days on screen or in the list's cache are
  attached (`attach()` from `ContentJournalDay`): their live text counts; the others are read from
  their files (`EditorHost::read`, cached until `fileChanged`), recounted 150 ms after a change.
  Bound: the first `dayLimit` days (1000) and 32 MB of text; past that the bar says "Searched the
  latest N days" and "In the whole notebook…" finds the rest. **Replace all** covers every day
  searched: the day written in through `NoteFind` (its undo step), the other attached days through
  their controllers (a step each), the rest written through `EditorHost::writeIfUnchanged` after the
  open editors saved — a file that changed meanwhile is left alone and counted in the toast; the
  toast's Undo (`StreamFind.undo(token)`) puts every day back while it still holds what was
  written. Days are created and destroyed as they scroll, each with its own controller and undo
  history.

**In the whole notebook** (`qml/views/ReplaceView.qml`, route `replace {query?, replacement?}`, palette
`go.replace`, More, SearchView's "Replace…", the bar's "In the whole notebook…"; not in the documentation
notebook). Core: `notebook/replace.h`; the app: `src/app/shell/replacesession.h`, `Workspace.replacing`.

* **Plan:** 350 ms after typing stops (Enter: at once), editors save (`EditorHost::saveEditorsRequested`)
  and `notebook::ReplaceScan` reads and searches on the index's worker threads (slices on WebAssembly), with
  a progress bar. Only notes: never `.trash/` or ignored paths, hidden files, conflict copies, drawings
  (`*.excalidraw.md`, or the plugin's frontmatter key), SVGs; a note that is not valid UTF-8 or is over 16 MB
  is left out and counted. The preview holds the first 50 hits of a note and 2000 in all, and says how many
  more there are: those go with their ticked note.
* **Choosing:** everything is ticked at first; a tick per note and per hit, tristate where partial;
  "untick all" also covers notes the scan finds later. Coming Back from a hit opened keeps the ticks
  for each note whose bytes, query and options are the same (a note changed meanwhile is ticked anew). A
  hit shows "line: before ~~match~~ → **replacement** after", from the note, so escaped into `StyledText`;
  "Inside a link: it will point elsewhere" under a hit in `[[…]]` or `](…)`: links are text here, keeping
  them is the rename's job. A tap on the text opens the note at that line.
* **Applying:** after a confirmation with hits and notes, in slices. Each file is read again; bytes that differ
  from the plan are refused and named in a toast. The hits are found again in that same text, then
  `Vault::writeTextIfUnchanged` writes with the History reason `replace`, which the 5-minute throttle
  never drops. BOM, line endings and all other bytes stay. Open editors follow through `fileChanged`.
* **Undo:** the toast's Undo (`ReplaceSession.undo(token)`, kept by the session, so it works from
  another view; the old text is held in memory until the next replace or notebook) restores only files
  still holding what was written, judged on the disk; others stay and are named, a deleted note is not
  brought back. A rescan running is stopped first. Replacing with the same text writes and tells nothing.

Tests: `tst_text_find`, `tst_notebook_replace`, `tst_markdown_render`, `tst_editor_controller`, `tst_editor_streamfind`,
`tst_editor_highlighter`, `tst_shell_workspace_wiring`, `tst_shell_workspace_editor`, `--selftest-shell`
(`checkFindAndReplace`), `tst_remote_control_qml` (the real Ctrl+F and Ctrl+H), `tst_content_qml`.

## Searching attachments (`src/app/attachments`, core `attachments`)

The whole-notebook search (route `search`, `SearchView`) finds words in the notebook's **PDFs**,
**documents** (DOCX, PPTX with its speaker notes, ODT, EPUB, spreadsheets in XLSX and ODS — a sheet a page, a row a line —, presentations in ODP) and **pictures** too, page by page, in the background and
offline. User guide: the documentation's page
"Search" ("Szukanie").

* **What is read** (`attachsearch::AttachmentIndexer`, owned by `Workspace`, QML
  `Workspace.attachments`): every PDF (`paths::isPdfPath`), document (`attachments::documentFormatOf`:
  `.docx`, `.pptx`, `.odt`, `.epub`; not `.docm`, the old `.doc`, spreadsheets) and picture (png, jpg,
  jpeg, webp, bmp — not SVG, GIF or icons) the vault lists, ignored paths and conflict copies left out. A PDF's own text
  (its text layer: `PdfReader::pageText`, poppler, Qt PDF, Android 15+) page by page; a page with fewer
  than 8 letters of its own (`MinTextLayerLetters`: a scan, a page number, a slide that is one
  picture) waits for **OCR** (text recognition), which reads it and keeps its few own words after
  OCR's. A picture is read only by OCR; without it — or in a format the device cannot decode (WebP
  without Qt's image plugins), or over 64 MB — only its file name is searched (nothing read, nothing
  hashed). A password-protected PDF is found by its name only; a file that is a link is never read.
* **Documents** (core `attachments/documenttext.h`, `extractDocumentText`) are ZIP files of XML, read
  on the same worker with no renderer and no OCR — so on every platform that has threads (the desktop,
  Android; not the browser) —, the whole file never in memory: miniz reads the archive through the
  file, only the parts needed are unpacked. **DOCX**: `word/document.xml` (the part `_rels/.rels` names
  as the office document) — `w:p` paragraphs with `w:t`, tabs, line breaks, text boxes —, then the
  footnotes and endnotes; the deleted text of tracked changes, field codes and the fallback copy of
  `mc:AlternateContent` are left out; the text is cut into **parts** of about 4000 characters at
  paragraph ends. **PPTX**: `ppt/slides/slideN.xml` in the order of `ppt/presentation.xml`'s slide
  list (else by their numbers), one **slide** a page (`a:p` / `a:t`; no speaker notes). **ODT**:
  `content.xml` — `text:p`, `text:h`, spaces, tabs, line breaks, footnotes and notes in the margin
  (not tracked changes' deleted text, not comments' authors and dates), parts as DOCX. **EPUB**:
  `META-INF/container.xml` → the package's spine → the XHTML files in that order, one page a file (one
  over 30,000 characters is cut at paragraph ends); paragraphs at block elements, no scripts, styles,
  head or SVG, HTML's named entities (`&nbsp;`) resolved. A hit's "page" is that slide, file or part.
  **Limits** (`DocumentLimits`): a file over 256 MiB is not read (name only, nothing hashed); at most
  10,000 entries and a central directory of at most 16 MiB (checked in the end record before miniz
  reads it); a part at most 32 MiB unpacked and all parts together 256 MiB; the text kept at most
  16 million characters; a part whose declared size its compressed bytes cannot give (stored: not
  equal; deflate: over ~1100:1) is refused before any memory is set aside for it (a "zip bomb"); an
  entry that is encrypted or not deflate is skipped; a password-protected ODT (`encryption-data` in its
  manifest) is locked; an encrypted DOCX is no ZIP (an OLE2 file). The XML is read by
  `QXmlStreamReader` — no external entity or DTD is ever read, and a part whose DOCTYPE declares
  entities is dropped (no "billion laughs", no XXE); a damaged or cut part keeps the paragraphs read
  before the damage. A file that is none of these is found by its name only, like a locked PDF.
* **In the background, never in the way:** one worker thread at the lowest priority there is
  (`QThread::IdlePriority`: SCHED_IDLE on Linux), started 3 s after the notebook's index is ready,
  one file at a time: the text of new and changed files first (newest change first), then OCR page
  by page — a file changed meanwhile goes before the next OCR page. A 100+ MB lecture scan costs one
  page's picture of memory, never the file (`PdfReaderPool` leases, given back every 16 pages, so
  Windows can move the file). It waits while a Nextcloud sync runs (`PdfReaderPool::idleSuspended`:
  the sync may replace, move or delete PDFs) and while **paused** (the search view's line, Settings →
  Search; a page in the middle of OCR stops within a fraction of a second and is read again on
  resume; the pause lasts until resumed or the app restarts). The GUI thread only merges what the
  worker found, 24 pages per event-loop turn (`AttachmentSearch::set`/`setPage`), and the search
  results follow 250 ms later (`contentChanged`, coalesced).
* **Cached on the device, never in the notebook:** `<app cache>/attachment-text/<sha256>.bat` — one
  record per content (core `attachments/record.h`: each page's text, where it came from — text layer,
  OCR with its languages, waiting for OCR —, OCR's words with their boxes; checksummed, a page read
  alone) — and the notebook's manifest `notebooks/<id>.manifest` (path → size, modification time,
  hash). An unchanged file is not even hashed again; a renamed, moved or copied one keeps its record
  (nothing read again); a changed one is hashed and read again. Records no notebook of the device
  names are pruned beyond 512 MiB (`DefaultCacheBytes`), oldest first; a newer extractor
  (`ExtractorVersion`) reads every file again. Settings → Search shows the size and **Clear** (with a
  confirmation): every notebook's attachments are read again.
* **The index** (core `attachments::AttachmentSearch`): a `SearchIndex` of its own — BM25+, the last
  word as a prefix, typo tolerance, diacritics optional ("zolw" finds "żółw") — with one document per
  page with text and one for the file's name ("wykład" finds "Wykład 3.pdf"). A hit is one row per
  file at its **best page**, with how many of its pages match; notes and attachments merge by the
  query's words typed as such (`SearchIndex::Hit::literal`), then by score. Excerpts come from the
  cache's records for the rows shown (a few pages of text kept in memory, `QCache`).
* **The search view:** a PDF's row has the file icon, its name, the excerpt and "Page 3 of 12 · 4 more
  pages match"; a document's "Slide 3 of 20 · 2 more slides match" (a presentation) or "Part 2 of 5 · 1
  more part matches" (`kind` "presentation" / "document" in the model, `page` the slide or part); a
  picture's "Picture"; text from OCR "Recognized text" (scan icon); a file found by
  its name only "Found by its name". The scope has **Attachments** beside All / Pages / Journals; the
  query's own filters narrow further (core `attachments/searchfilters.h`): `in:attachments`,
  `in:pdf`, `in:documents` (also `in:docs`, `w:dokumenty`), `in:pictures`, `in:notes`, `in:pages`, `in:journals` — also `type:`, Polish `w:` / `typ:`
  and Polish values (`w:załączniki`, `w:obrazy`, `w:notatki`, `w:dziennik`; diacritics optional),
  several add up; a word only looking like one (`12:30`, `https://…`, `in:xyz`) is searched. While
  attachments are read, a line under the filters says so ("Reading attachments… 3 of 40",
  "Recognizing text: page 4 of 120 in “skan.pdf”", "waits for the sync") with **Pause** / **Resume**.
* **Opening a result:** a PDF opens the **PDF viewer at the page** with the search typed — route
  `{view: "page", path, pdfPage, open: "search", search: find}` — and the first hit at or after that
  page is the current one, highlighted ("PDF viewer" → Search). `find` is what shows the hit there:
  several words when the page holds them in a row, else the page's own spelling of the best matched
  word ("Żółw" for "zolw"). A scanned page has no text of its own: the viewer's search reads its OCR
  text (`PdfViewModel::setOcrLookup`, set by `Workspace`: the record's page when the cache was made
  from the file's very size and time) and places the hits by OCR's word boxes. A picture opens with
  `search`: `ContentPictureView` frames the places of the words on the picture (accent frames over
  the picture at any size; `Workspace.attachments.matchRects` — OCR's boxes on the picture as shown:
  a photo stored sideways is shown and read by OCR upright, by its EXIF orientation),
  "2 places" with × in the bar (wide views), and **Recognized text** shows what OCR read, read-only and
  selectable ("It may hold mistakes"). A document opens its file the way any other attachment does
  (PageView `fileRoute`, "Other attachments": read-only, **Open in another app**); the row's excerpt is
  what shows the passage.
* **OCR** (`platform/ocr.h`): Tesseract 4 or 5 loaded at run time through its C API (no build
  dependency): the Nix package and dev shell bring Tesseract 5 with the English and Polish "fast"
  models (`tessdata_fast`, 9 MB); the AppImage and Linux builds use the system's
  (`libtesseract.so.5/.4`, models in `/usr/share/tesseract-ocr/…`, `TESSDATA_PREFIX`); Windows a
  Tesseract installation (UB Mannheim's installer in Program Files, `libtesseract-5.dll`); a folder of
  the user's own models (`<app data>/tessdata`) is looked in first (`platform/ocrdata.h`). **Off by
  default** (Settings → Search, "Recognize text in pictures and scans"): a page costs a core about a
  second (docs/PERFORMANCE.md "Searching attachments"). Its languages: **Automatic** — the UI language
  and English — or chosen (chips of the models installed, by their own names); a change reads the
  recognized pages again in the new ones. Words below 40 % confidence are left out (a photo without
  text gives no noise). A PDF page is drawn at ~300 dpi (its long side 1000–4200 px); a picture is
  read as taken (EXIF orientation applied), at most 4200 px and 24 MP, a small one (a screenshot)
  twice its size. Where OCR is not available, Settings says why and what to install; the PDF viewer's
  "no text to search" adds "Turn on text recognition in Settings → Search" where OCR could read it.
* **Platforms:** the desktop (Linux, Windows): text layers, documents and OCR as above. **Android:**
  documents everywhere; PDFs' text on Android 15+ (PdfRenderer's text API; older versions find PDFs by name only); no OCR — Tesseract
  through the NDK would add some 25 MB per ABI and its models to the APK (docs/ANDROID.md); Settings
  says so. **The browser:** no PDF renderer and no threads: attachments are not read (Settings:
  "This version of Brulion reads no PDFs and recognizes no text"; `documentTextAvailable` false).

Settings → **Search** (`qml/attachments/AttachmentSettings.qml`): "Search in attachments" (on by
default; `AppSettings.attachmentSearch`), "Recognize text in pictures and scans (OCR)" (off;
`attachmentOcr`), "Recognition languages" (`attachmentOcrLanguages`: "auto" or codes "pol+eng"),
"In this notebook" (files read, pages with text, what is being read, Pause / Resume), "Text kept on
this device" (its size, Clear). Device settings, for every notebook.

Tests: `tst_attachments` (core: records — round trips, a page alone, malformed and random bytes —,
the manifest, the index — pages, names, OCR pages, renames, kinds, excerpts, the viewer's text —, the
filters), `tst_documenttext` (core: DOCX / PPTX / ODT / EPUB built in the test from their XML with
Polish text — reading order, text boxes, footnotes, what is left out, named entities, pages —, the
record and the search of it; hostile and damaged files: a "zip bomb", forged sizes, too many
entries, a file over the limit, the unpacked total and text limits, encrypted and non-deflate
entries, entities that never expand or read a file, truncated archives and XML, random damage,
cancelling), `tst_ocrdata`, `tst_ocr` (Tesseract on `tests/data/attachments`: a whiteboard, a photo
stored sideways, a scanned PDF page; skipped without Tesseract), `tst_attachmentindexer` (the
indexer on a notebook: text layers, the cache reused, renames, changes, OCR, pause, languages, the
viewer's lookup; `documents`: a notebook of DOCX / PPTX / ODT / EPUB files read with no PDF renderer,
cache reused, renames, changes, damaged / encrypted / bomb-like / too large files by name only;
`largePdf` measures — BRULION_ATTACHMENT_BENCH=1), `tst_content_models` (the
search model's attachment rows and filters), `--selftest-shell` (`checkAttachmentSearch`: a PDF's
page found and opened highlighted, a picture's recognized words framed).

## Search by meaning (`shell/semanticsearch.h`, core `semantic`)

With **search by meaning** on, the whole-notebook search also finds notes by what they mean, not
only by their words: "apple cake" finds "Szarlotka babci", an English query a Polish note,
"kupiłem opony" the note "Kupić oponę" (no stemmer needed). A small multilingual embedding model
runs on the device; nothing leaves it. User guide: the documentation's page "Search" ("Szukanie"),
"Search by meaning". Design, model and measurements: docs/SEMANTIC.md.

* **Off by default** — Settings → Search → **Search by meaning** (`qml/shell/ShellSemanticSettings.qml`,
  `Workspace.semantic`, the device setting `search/semantic`). Turning it on downloads the model when
  it is not here yet: Multilingual E5 Small (Microsoft, MIT), 8-bit GGUF, 126 MB (132 million bytes)
  (`semantic::semanticModels()`), into `<app data>/models/semantic` with `ai::LocalModelFiles` — the
  same resumable, checksummed download as the assistant's and the speech models (Download / Stop /
  Remove, the progress, "Checking the downloaded file…"). **Remove** (with a confirmation) deletes the
  file and every notebook's vectors on the device and turns it off. Hidden where the build has no
  plugin (`available`: the browser, a build without BRULION_LOCAL_AI) — search is full text there.
* **What runs it:** the plugin `brulion_semantic` (core `semantic::SemanticRunner`: llama.cpp of
  3rdparty/llama, CPU), loaded the first time a note or a query is embedded and kept next to the
  model runner's plugin — the app links no llama.cpp. The model leaves memory five minutes after its
  last use. A model that cannot be opened stops it with the state "failed" and the reason (Settings
  says it; off and on again tries anew); search is full text meanwhile.
* **Notes read in the background** (`SemanticSearch`, owned by `Workspace`): a few seconds after the
  notebook's index is ready, every note is compared with the vectors kept for it — each note's
  `semantic::noteKey` (FNV-1a of its title and text) — and the new and changed ones are embedded on a
  worker thread at the lowest priority (`QThread::IdlePriority`: SCHED_IDLE on Linux), the newest
  journals first, up to 16 notes / 8,000 characters per call of the model. A note is cut into chunks
  of ≤ 120 words (`semantic::noteChunks`: encrypted fragments blanked, frontmatter skipped, at most
  256 chunks). Edits follow the notebook's index (`VaultIndex::changed` + `changesSince`, 1.5 s after
  the typing stops): a changed note is embedded again first, a deleted one forgotten. While notes are
  read, the search view says "Reading notes by meaning: 120 of 900" under the filters, and Settings
  ("In this notebook") the same or "900 notes can be found by meaning"; a note not read yet is found
  by its words alone meanwhile.
* **Kept on the device, never in the notebook:** `<app cache>/semantic/<notebook id>-<model>.vectors`
  (`VectorIndex::save`: half-precision vectors, ~770 bytes a chunk, checked against the model, the
  format and the chunker's version — anything else is built again), written when the queue is empty,
  every minute of a long build and when the notebook closes. Opening the notebook again reads it on
  the worker thread; only the notes changed since are embedded.
* **Calibration** (core `semantic::Calibration`): the model's shipped query mean, the mean of the
  notebook's chunks and its Polish–English direction, estimated from the notebook whenever the index
  is up to date after a change; applied to queries of up to five words.
* **The search** (`SearchResultModel`, so the search view and the quick switcher's "Search the notes
  for …"): the full-text hits of notes and attachments are fused with the notes closest in meaning
  (core `semantic::hybridSearch`: 0.8 meaning + 0.2 words, each min-max scaled — never meaning
  alone; a PDF, a picture or a note not read yet ranks by its words alone). Every full-text hit stays;
  of the notes found by meaning alone the best 20 (`MeaningAloneHits`) join them, marked **Close in
  meaning** (sparkles icon; `byMeaning`), their excerpt and line where their closest chunk starts. A
  query's vector is embedded on a thread of its own with a context of its own (a query never waits for
  the indexing; ~10–20 ms on a laptop's CPU, 6 ms on a Pixel 9 Pro): the list waits for it up to
  150 ms (`MeaningWaitMs`), else shows the full-text order and is re-sorted when it comes; the last 32
  queries' vectors are kept. Scopes, folders and `in:` filters apply as before (`in:pdf` asks nothing
  of the model).
* **Platforms:** the desktop and Android (64-bit) have the plugin; **the browser** has none — search
  is full text there. brulion-cli: `search --semantic` reads the same vectors (docs/CLI.md).

Tests: `tst_semantic` (core: chunks, calibration, the vector index — incremental changes, compaction,
the calibrated search against calibrated vectors, save/load and damaged files —, keys, the diff,
encrypted text masked, fusion with notes the index lacks, the hybrid search, the model's entry),
`tst_content_semantic` (the app with a fake model: full text without the plugin, the model or the
setting; the notebook embedded and fused; edits, deletions, the vectors kept and read back; a save
still on the index thread written by the close and never after it; a failing model; removal; the
download), `tst_semantic_embedder` (the real model through llama.cpp,
skipped unless `BRULION_SEMANTIC_MODEL` names its GGUF file).

## Spell checking

Misspelled words are underlined in red (`Theme.danger`), in Polish and English — both at once by
default — with corrections in a menu: in the edited block, in source mode and (Settings → Editor →
"Underline in the whole page") in the rendered blocks. User guide: the page "Spell checking" of the
documentation notebook.

* **Where:** the prose of the edited block (`InlineEditor` of text blocks, headings, quotes,
  callouts: `spellChecked`), of source mode's text (`SourceEditor`, below) and of the rendered text
  of those blocks (`BlockText`, below); code, `calc`, `query`, formulas, table cells and the
  properties panel are not checked. `spell/spellwords.h` (core) finds the words of a line and
  skips the syntax: code spans and comments, formulas, wiki links, embeds and block refs whole, a Markdown link's
  destination (its text is checked), URLs, e-mail addresses, tags, every task and date token (📅 ⏰ 🔁
  and its rule, priorities, task ids, Logseq planning), block ids, `key::` names, task keywords,
  callout types, HTML, `{{macros}}`, footnote markers; words with digits or `_`, single letters,
  acronyms in capitals, mixed case (`iPhone`), file names, paths. Hyphenated words are right whole or
  by their parts (an acronym part counts: `PDF-y`), `np.` with its dot, typographic apostrophes as
  ASCII (`spell/spellchecker.h`).
* **Underline:** `MarkdownHighlighter.spellCheck` (the `SpellCheck` singleton while the block is
  edited) merges the format "spelling" — a plain underline in `spellColor` (Qt Quick 6.11 draws no
  wavy `SpellCheckUnderline`). The word ending right at the caret (`spellCaret`) is not marked while
  it is typed; it is once the caret leaves it.
* **Never blocking:** `SpellCheck` (`src/app/editor/spellcheck.h`) keeps a verdict per word and runs
  `spell::SpellChecker` on a worker thread of its own, made for the first word asked about; loading
  a dictionary (Hunspell's Polish one: a few hundred ms) and Android's checker (IPC) happen there. A word seen
  for the first time is Pending: queued, sent in batches of 200 after the highlighting pass, and
  only the lines waiting for it are highlighted again when the answer comes (`checked(changed)`).
  Verdicts are cached (20 000 words), so retyping asks nothing.
* **Only while it is used:** the worker and its dictionaries (Hunspell's Polish and English ones
  hold about 23 MB) go once no word was asked about for a while — 30 s after a word of the edited
  text (`SpellCheck.idleMs`: typing asks again soon), 8 s after the words of rendered text
  (`readIdleMs`, `wrongWords()`: a page was opened and is read now) — and when the app is away
  (`unload()`: "Navigation", Away). The verdicts stay, so nothing is underlined anew and nothing
  asked again; the next word not seen before brings the worker back with the same settings and
  personal words (its verdict comes after the dictionaries are read again: the Polish one takes
  0.08 s of a laptop's processor, on the worker).
* **Menu:** a right-click on a marked word (`BlockEditor.openTextMenu`, `InlineEditor.misspelledAt`)
  puts its corrections (up to 6, asked from the worker: "Looking for corrections…" until they come, "No
  corrections") at the top of Cut / Copy / Paste, then **Add “…” to dictionary** and **Ignore**.
  A correction replaces the word through `PageController.replaceRange` (one undo step, the caret
  after it). On touch UIs a tap on a marked word opens only those items (`openSpellingMenu`; the tap
  places the caret as usual — a long press stays the system's selection). Adding or ignoring asks
  again only about the words marked wrong, which stay marked until the answer (no flicker); taking a
  word out of the dictionary or changing languages forgets every verdict (`invalidated()`).
* **Source mode** (Ctrl+E): `SourceEditor`'s `MarkdownHighlighter` ("source" mode) underlines the
  prose with the same tokenizer and skips, besides, whatever spans lines — fenced code (` ``` ` and
  `~~~`), the frontmatter (keys and values: the properties panel is not checked either), display
  formulas between lines that are only `$$` (also in list items and quotes; the bits of a Normal
  line state carry it: `MathFlag`), `%%` comments over lines (the text after the closing `%%` and
  before an opening one is prose). Each line keeps its prose range (`SpellRange` in the block's
  user data) so `misspelledAt()` never offers a word of the syntax. A right-click on a marked word
  — a tap on touch UIs — opens the text menu with only the spelling's items
  (`BlockEditor.openSourceSpellingMenu`); a correction replaces the word through
  `EditorInput.replaceRange` (one step of the TextEdit's own undo, the caret after it). Never a
  stall on a large page: a large edit (over 20 000 characters: the page's text set, a long paste)
  is highlighted at once without spelling, a pass over its lines (`sweepSpelling()`) adds it in
  slices of 6 ms with as long a pause between them; lines waiting for verdicts are highlighted
  again once every word asked for has its answer (`SpellCheck.idle()`: one pass, not one per
  batch of 200), also in slices; a change of languages or of the dictionary is such a pass too.
  Only the lines on screen and 100 around them are checked (`spellViewFrom` / `spellViewTo`, bound
  to the view's scroll position; the lines that scroll in are checked as they come, those that
  leave keep their marks). Typing highlights the edited line only (QSyntaxHighlighter). The pause
  matters:
  QTextDocumentLayout lays out every format change made outside an edit's own signal on its own and
  walks all the document's lines for it (~1 ms per changed line at 10 000 lines), so only lines whose
  marks change cost that, spread over the pass (docs/PERFORMANCE.md "Spell checking in source mode").
* **Rendered blocks** (`SpellCheck.wholePage`, "Underline in the whole page"): `BlockText` asks
  `PageController.spellMarkedHtml(row, html, SpellCheck.revision, color)` — computed only for the
  delegates that exist (the blocks on screen and the list's cache), never for hidden rows. It takes
  `SpellCheck::wrongWords(body)` (the block's Markdown, line by line as in the edited block;
  cached by the text, `MaxCachedTexts`, until a verdict changes or every verdict is forgotten —
  `revision` changes then and the bindings follow) and puts the words on the rendering
  (`spellmarks.h`, `BlockListModel::spellMarkedHtml`): through the source anchors of the traced
  rendering (`renderBlockTraced`, kept per row while the HTML is), each verbatim piece of a word
  gets `<span style="text-decoration:underline;text-decoration-color:…">` — plain text, emphasis,
  headings, quotes, callouts; a word inside a construct rendered as a whole (a Markdown link's
  text) is not underlined until the block is edited. The marked HTML is kept per row while the html
  and the words are the same. A text with marks is shown by the tinted TextEdit (as a text with
  chips): a right-click on a marked word maps the TextEdit's position through the rendering's
  source map (`RenderedText::displayRange`, `PageController.misspelledInRendered`) and opens the
  spelling's items (`BlockEditor.openRenderedSpellingMenu`); a correction is
  `PageController.replaceInBody(row, from, to, text, replacement)` — the block's body through
  BlockDocument, one undo step, refused when the body no longer has the word there. On touch UIs
  the marks show, a tap still edits the block (its word then has the edited block's menu). Not
  drawn by the RenderedTextItem (BRULION_TEXT_ITEM), JournalDayItem or PageItem, nor in
  transclusions and query results.
* **The page drawn by Atrament** (Settings → Editor → "Draw the editor with Atrament";
  docs/ATRAMENT.md M2 step 2, `src/app/editor/atrament/pagespell.cpp`): the same words by the same
  rules — the edited block's prose by the highlighter's `SpellRange`s, the word at the caret left
  alone while it is typed; the drawn blocks' with `wholePage`, only where shown as written — under a
  wavy line in `Theme.danger` (Atrament draws one; Qt Quick's is straight). A right-click on a
  marked word in a drawn block, or a tap on one in the edited block on touch UIs, opens the same
  spelling items (`BlockEditor.openAtramentSpellingMenu`); a right-click in the edited block opens
  its text menu with the word's corrections at the top (`BlockEditor.openAtramentTextMenu`, as
  InlineEditor's).
* **Settings** (Settings → Editor, shown only where `SpellCheck.available`): "Check spelling"
  (`AppSettings.spellCheck`, default on), "Underline in the whole page" (`spellWholePage`,
  `spelling/wholePage`: default on, off on Android — every word on screen would go to the system's
  checker over IPC), "Spelling languages" (`spellLanguages`: Automatic — the UI
  language, the system's and English, `spell::resolveLanguages`; Polish, English, Both — of the
  languages this device has) and "Personal dictionary" (a dialog listing `SpellCheck.personalWords`:
  add, remove). main.cpp binds `SpellCheck` to these settings and to the UI language.
* **The personal dictionary is a device setting** (`spelling/words`, every notebook of this device),
  not a notebook file: Nextcloud sync carries only `.brulion/config.json` of the `.brulion/` folder,
  and in that file a key-wise merge would drop the words one of two devices added at the same time.
  "Ignore" lasts until the app quits. Matching follows Hunspell's letter case: "Kraków" also
  accepts "KRAKÓW", "kot" also "Kot", never "kraków" for "Kraków".
* **Backends** (`src/platform/spellbackends.h`, one per build): **Hunspell** on the desktop — the
  `.aff`/`.dic` pairs found by `spelldictionaries.h` (DICPATH, `<exe>\dictionaries` on Windows,
  `<exe>/../share/hunspell` in the AppImage, the build's `BRULION_SPELL_DICTIONARY_DIRS` — the Nix
  package's store paths of `hunspellDicts.pl_PL` and `en_US` —, XDG data folders, `/usr/share/hunspell`);
  pl_PL is preferred for Polish, en_US, en_GB, … for English; the dictionary's 8-bit encoding (the
  Polish one is ISO8859-2) through `spell/dictencoding.h`. **Android**: the system's spell checker
  (`TextServicesManager` / `SpellCheckerSession`, the keyboard's dictionaries; `SpellCheck.kt` over
  JNI, one session per language, at most 3 s per answer — none: nothing marked; it checks words,
  not a text field, so the rendered blocks use it the same way when the setting is on); its languages are
  the checker's subtypes (Android 12+) or the device's languages and English; with the checker off
  in Android's settings Brulion offers no spell checking. **None** in the browser and in desktop
  builds without Hunspell (`-DBRULION_SPELL_BACKEND=none`): the settings hide it. Packaging: the Nix
  package and dev shell (flake.nix, `DICPATH`), the AppImage (Ubuntu's libhunspell, hunspell-pl,
  hunspell-en-us), Windows (Hunspell compiled in from its release and LibreOffice's dictionaries,
  downloaded by `scripts/windows-build.ps1` with pinned SHA-256s).

Tests: `tst_spell_words` (the tokenizer), `tst_spell_checker` (rules, languages, encodings),
`tst_spelldictionaries`, `tst_spellbackends` (Hunspell with ISO8859-2 and UTF-8 dictionaries the
test writes), `tst_editor_spellcheck` (the worker, the settings, the underlines, the caret,
`misspelledAt`; source mode's exclusions, data-driven; a large text in slices, no turn of the event
loop over a frame; `wrongWords()` and its cache; the marks on the rendered HTML; the word under a
click, a correction in a rendered block and its undo), `tst_editor_qml_spelling` and `_touch`
(`--editor-script spelltest`: the mark, the menu with a right-click and a finger, corrections, undo,
add, ignore, syntax, off; a rendered block's mark, menu, correction and undo — desktop —; source
mode's marks, menu, correction, Ctrl+Z and Ignore).

## The block editor (heart of the app)

A page is edited as a list of **blocks** (Logseq model), backed by `BlockDocument` in the core
(each block keeps its raw Markdown so untouched blocks are written back byte-for-byte):

* `BlockListModel` (C++, `QAbstractListModel`) exposes the blocks with roles such as `raw`,
  `editText`, `html` (rendered by the core `InlineRenderer`), `depth`, `kind`, `view`,
  `headingLevel`, `hasChildren`, `collapsed`, `taskStatus`, `blockId`, `lang`, calc results,
  table models… Zoom (one block's subtree) and folding are model-level; rows survive edits
  (no model resets).
* `qml/editor/BlockEditor.qml`: a `ListView` of `BlockDelegate`s. A delegate takes the roles every
  block reads as typed `required property`s (`shown` is the role `visible`); the roles only some
  blocks or views read — the rendered `html`, `editText`, a table's or chart's data, a query… —
  come from `BlockDelegate.rowData` (`BlockRowData`, `src/app/editor/blockrowdata.h`: typed,
  read on demand from the delegate's model object, so a hidden row or a code block never renders
  a text it does not show). An unfocused block shows its
  rendered rich text (links: `page:`, `part:` (a path part of `[[Folder/Page]]`), `tag:`, `block:`,
  `ext:`, `task:`, `reminder:` URLs; the token menus' `date:`, `recur:`, `prio:` and `day:`). A text with tinted spans (the renderer's
  chips and tags, inline code, highlights: a `background-color` in its HTML) is shown by a
  read-only rich-text `TextEdit` instead of a `Text` (`BlockText`): `RichTextPills`
  (`src/app/editor/richtextpills.h`) gives its document the same fixed line height, takes the
  spans' square background brushes off in the document's own change signal — before the next
  frame — and keeps their colour and kind (a span with its own text colour is a pill, a
  fixed-pitch one or any other a box) on the format, and lays one rectangle per line segment
  out of the document's own layout, centred on the span's glyphs (its font's ascent and descent
  around the baseline); `BTintShapes` draws them under the text (a Repeater over `plainShapes`,
  another over `iconShapes`: typed `richTextPill`s). Every other text stays a `Text`
  (cheaper rows); clicks, long presses, right-clicks and the cursor shape go through the same
  overlay for both (`BlockText.linkAt`).
  **Rendered text outside the blocks** — linked references (`ContentRefRow`), Tasks and calendar
  rows (`TasksRow`), embeds (`TransclusionView`), table cells (`TableBlockView`), query results
  (`QueryBlockView`), link previews (`BLinkPreview`) — is a `BRichText`
  (`qml/components/BRichText.qml`): a plain `Text` for a text without tints that fits its line
  limit, else a read-only rich-text `TextEdit` whose `RichTextPills` gets the HTML itself (`html`,
  with the TextEdit's `font` and `width`) and, with `maximumLineCount`, cuts the document after that
  many lines and ends the last one with "…" (no link, no tint, the text's own colour; shortened by
  whole characters until it fits; `truncated`) — QML `Text` ignores `maximumLineCount` and `elide`
  for rich text, so before this these rows were never cut at all. A new width or font sets the
  whole text again and cuts it anew; created from QML it does so once, when it is completed
  (`QQmlParserStatus`), so the row has its final height when the list places it ("Scrolling
  performance": rows of tinted or long texts cost a TextEdit, the others stay a Text). Its height is
  counted in whole fixed-height lines; its implicit width is the plain `Text`'s, which a tinted text
  leaves empty (not laid out twice) unless `measured` asks for it; its TextEdit takes no input (`enabled: false`): the row's
  handlers ask `BRichText.linkAt`. Limits: linked references 12 lines (6 in the side panel), Tasks
  and query results 4, a table cell 1 (its tooltip shows the whole text when cut), embeds and
  previews none. Tasks' text links take the presses on them through an item whose
  `containmentMask` is the links (a press elsewhere is the row's).
  Clicking or tapping it edits it inline (`InlineEditor`: a `TextEdit` with the raw Markdown,
  minus the task marker / fences the delegate draws, and `MarkdownHighlighter`).
  * **Links in rendered text: path parts, hover, accessibility.** A wiki link to a page in a
    folder without display text (`[[Firma/Adam cośtam]]`) is rendered as one anchor per path part
    (docs/SYNTAX.md §2 "Path parts"): the parent parts are `part:<k>:<target>` links coloured by
    what they open (`RenderContext::resolveLinkPart` → `EditorHost::linkPart` →
    `VaultIndex::resolveLinkPart`; a new page in the unresolved colour), a muted `/` as written
    between them (no link: a click there edits), the last part the `page:` link as before. A click
    or tap on a part navigates to `{view: "page", target, linkTarget, linkPart, from}`, which the
    shell's `Workspace::resolveRoute` turns into the page, the folder's All pages or a new page
    (the resolved route carries no part keys: history and the side panel see ordinary routes).
    The link under the mouse — and under the finger or button while it presses (a phone's
    feedback; a flick lifts it) — is underlined, that anchor only: `hoveredLink` +
    `TextHelper.hoveredLinkHtml` (`InlineRenderer::withHoveredLink`) on the shown text only (not
    on chips, tags, the task checkbox or the chips a click edits). With a screen reader on
    (`DeviceInfo.screenReader`: `QAccessible::isActive()`, also UiAutomator; self-tests set
    `DeviceInfo.simulateScreenReader`, manual runs `BRULION_SIMULATE_SCREEN_READER=1`) every
    anchor of rendered text — each part too — is an `Accessible.Link` item of its own
    (`BLinkTargets` + `RichTextLinks`: the anchors' rectangles from a `TextEdit`'s document or the
    `Text`'s HTML laid out again), named by its text, with a focus ring; its press action does
    what a click does. Nothing of it exists without a screen reader (an inactive `Loader` per
    text; the layer, laid out once all its properties are set, comes and goes with the reader).
    Alt+Enter on the edited raw line follows the link (part) at the caret
    (`PageController.linkAtCursor`, the shortcuts above). The same in table cells, query results, embeds (`TransclusionView`), the properties'
    text values (`PropertyRow`), Tasks and calendar rows (`TasksRow`) and linked references
    (`ContentRefRow`); the embed frame's header and list-property chips stay single links.
  * **Link previews** (`LinkPreviews.qml`, a QML singleton; `BLinkPreview.qml`; C++
    `LinkPreview`, `src/app/editor/linkpreview.h`). The mouse pointer resting on a `page:` or
    `block:` link (`[[Page]]`, `[[Page#Heading]]`, `[[Page#^id]]`, `((id))`; in the editor also a
    day's `day:` link, by the page it names: `PageController.dateChip(…).url`) for 600 ms opens a
    popup under the pointer (above it near the window's bottom) with what the link opens, rendered
    as an embed of it would be: `BlockListModel::linkPreview()` builds the transclusion map of
    `![[Page…]]` / `!((id))` from the note the link is written in (InlineRenderer's escaped rich
    text, never the note's own HTML; nested embeds depth-limited, at most `MaxTransclusionBlocks`
    blocks, the rest counted), read through the text's `EditorHost` (the shell's `Workspace`, the
    editor's host) when it opens — so it shows the saved note. Shown with `BRichText` (rounded
    tags and chips), scrollable (at most half the window, 360 px), tasks with a read-only
    `TaskBox`. Every view that shows rendered text calls `LinkPreviews.hover(item, link,
    sourcePath, host, x, y)` from its `HoverHandler` and `LinkPreviews.leave(item)` when the
    pointer leaves: `BlockText`, `ContentRefRow`, `TasksRow`, `TransclusionView`, `TableBlockView`,
    `QueryBlockView` (nothing per row: one popup per window, created when it first opens, in the
    window of the text). It stays while the pointer is on the link or on the popup and closes
    350 ms after it left both, on a press elsewhere (`CloseOnPressOutside`, the press goes on to
    what is under it), when the text it belongs to is destroyed (a row scrolled away, the page
    closed, `anchorItem` turns null) or the block is edited; another link moves it after the
    delay. It never takes the keyboard focus. Its header (icon, title — `missingPage` in the
    unresolved colour) opens the link (`LinkPreview.openLink()`: the host's `navigate()`, as a
    click in a note; Ctrl+click or a middle click: a new tab — "Tabs, split view and windows"), a click on a link inside follows that link from
    the note it is written in; task boxes, reminders and the chips a click edits do nothing there.
    A page not written yet says "“%1” does not exist yet. Click the title to create it." (a click
    on the title creates it, as any link to a missing page), a missing section or block says so,
    an empty page "Nothing here yet."; links to attachments (pictures, PDFs — by the target's
    extension, found or not) and drawings have none. **Touch UIs have no previews** (no hover;
    `Theme.touchUi`): a long press stays the link's menu, whose "Open in side panel" shows
    the page beside the note on wide screens. Tests: `tst_editor_linkpreview` (pages, sections,
    blocks, missing ones, links without a preview, escaping, malformed URLs, the routes followed),
    `--selftest-shell` (`checkLinkPreviews`: real mouse moves and clicks).
* `PageController` executes commands (one undo step each, typing coalesced), saves debounced
  and on focus loss / suspend / quit, and merges external changes three-way. Its file deleted (the
  trash, another program, a sync) with nothing unsaved, it shows the page as one not written yet —
  a journal day stays on screen without the deleted text —; unsaved edits are kept and saved
  again (`removedFileShowsEmptyPage`, `removedFileKeepsUnsavedText`). An undo step is a
  `BlockDocument::Delta` — the run of blocks the command changed, a block in both versions with
  only its changed range of text (`undoStoresChangesNotPages`, `pasteIsASmallUndoStep`) — and
  undo / redo apply it as a document operation through `BlockListModel::apply()`
  (`PageController::applyStep()`: `BlockDocument::revert` / `reapply`): nothing is read again,
  only the changed blocks are parsed, every block keeps its uid. Rows of unchanged blocks stay,
  a changed block keeps its row (`dataChanged`: its delegate, fold and a tracked handle survive —
  `undoKeepsChangedRowsInPlace`; an inline editor open on it gets the step's text; a table cell
  being edited is closed before the step — its field would keep the old text and write it back —
  and the cell toolbar's Undo / Redo open it again), only blocks the step adds or removes
  come and go, never a reset; undo costs about what the command cost (`largePageTouchesOneRow`
  compares their medians). Typing merges into one step per block (`BlockDocument::composed`); a
  keystroke that also changed blocks far away — typing at the top of a page renumbers a loose
  ordered list further down — stays a step of its own (`typingAfterAFarChangeIsAStepOfItsOwn`).
  A keystroke costs the edited block, not the page (docs/PERFORMANCE.md "Typing: a keystroke costs
  the block"): `BlockListModel::apply()` refreshes only the edited rows and the rows that follow
  every change of the page (its `dependents`: calc blocks, tables, charts, `((id))` and inline
  calculations, embeds of this page; the ancestors of a block whose task status changed) when the
  edit kept the blocks' kind, depth, children and lines, and tells the views only the roles of the
  block's text; `markDirty()` compares the edited block's own text with the file's text around it
  (`PageController::TypingSave`) instead of the page; `BlockText` keeps the rendered text of the
  edited block as it was (the editor shows it) and `InlineEditor` updates the completion once per
  keystroke. `tst_editor_typing` compares both model paths after every step of scripted and random
  edits. The caret goes back to the block and the place it had — also when the
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
  (`zoomOutKeepsTheEditedBlockShown`). Back (Android's key or gesture) zooms out one level, as the
  breadcrumb's arrow (`zoomOut()`: to the zoomed block's parent), before it navigates — the
  editor's `canStepBack` / `stepBack()`, through its view ("Navigation"); a journal day zoomed
  into becomes the day written in, so its zoom is the one Back takes.
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
  are a block id line (`^id`, written by "Copy block reference" and `((` completion) and
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
* Slash commands (`/`, Polish and English names, diacritic-insensitive filter), every one of them
  listed (the other lists stop at `SuggestionModel.limit`), in this order (`slashCommands()`,
  blocktext.cpp): a first screen (5 rows on touch, 8 on desktop) of the kinds used most —
  Task: to do, Heading 1, Heading 2, Table, Reminder…, Due date…, File… (any files as links),
  Image… —, then text blocks — Code block, Quote, Callout, Heading 3, Divider —, dates — Alarm…,
  Scheduled…, Repeat…, Today, Tomorrow, Date…, Current time, High / Low priority —, inserts —
  Picture from the internet…, Drawing (a new drawing, opened in the drawing editor), Drawing on a
  picture or PDF… (drawings on a picture or a PDF's pages: "Drawing editor" → Backdrop), Embed,
  Block reference, Template…, Table from CSV… —, data — Calculation, Query, Diagram, Chart, Math
  formula — and last the other task statuses, Task: in progress / done / waiting / cancelled /
  no status (in Polish "Zadanie: do zrobienia" … "Zadanie: bez statusu"; also found as todo,
  doing, done, czeka, odłożone, waiting, anulowane, cancel, bez statusu…). A filtered list ranks
  by the match (a name's start, a word's start, anywhere in it; keywords after names), then by
  that order: "/zad" lists the six statuses as the status menu does, "/załącznik" File… before
  Image…. Under each name: the syntax it writes ("⏰ date time", "- [ ]
  task", "```calc block"). A status command sets the status of the block being edited
  (`PageController.applySlashCommand` → `setTaskStatus`) in the same undo step that removes the
  "/command": a block that is no task becomes one (a paragraph a list item), Logseq keywords keep
  their workflow (TODO → DOING, LATER → NOW), "no status" removes the checkbox and its ✅ / ❌
  date, and the caret stays in the words around the command.
* **Block menu** (`BlockMenu`, `qml/editor`; one per editor, made when first opened, the block
  tracked while it is open): **Copy**, **Cut**, **Copy as plain text** (the block with its
  children, which becomes the selection — or, for a block that is part of a selection of several,
  "Copy 3 blocks", "Cut 3 blocks": the whole selection; indenting, moving and "Delete 3 blocks"
  then act on all of it too — a selection with gaps is not indented or moved: those items are
  shown disabled, a tap says why), **QR code…** (the same blocks as QR codes: "QR code"),
  **Select** (the block becomes the selection; on touch UIs the
  selection mode), Zoom in and Collapse / Expand (outline), **Copy link** ▸ —
  Block reference `((…))` (Ctrl+Shift+C), Link to block `[[…#^…]]`, on headings Link to
  heading `[[…#…]]`, Embed (`!((…))`, a heading's section `![[…#…]]`); what each writes:
  docs/SYNTAX.md §2 "Headings and blocks in links" —, Insert picture…, Insert drawing, Indent / Outdent
  (outline, list items in a document), Move up / Move down, Delete block. It opens for every
  block in both note modes: a right-click anywhere on a block — text, heading, a code, calc, table,
  query or callout frame, an embed — with a mouse, touchpad or stylus, never a finger (one
  `TapHandler` on the editor's list, not one per block, picking the block drawn under the pointer
  — `BlockEditor.rowAt`, also the page title drawn above its properties —; the page properties
  have none), the bullet or an ordered item's number (a right-click — their press is theirs, the
  list's handler does not open it twice — or a long press), and on touch UIs the editing
  toolbar's **More** (the menu right above the bar; in a table cell the cell tools' More ends
  the cell's editing, selects the table and opens its menu at it). A right-click on a token (a date,
  a reminder, 🔁, a priority, a link to a day) or a link opens its own menu instead (below, "Token
  and link menus"). A long press on text opens no block menu: it selects the block (on a token or
  a link: its menu) (the touch selection mode, "Selecting and copying"). The submenu opens beside the menu with a mouse, in place of it on touch UIs (`BMenu`:
  `cascade` off). Every copy says "Copied …", or why nothing was copied — no clipboard (the
  text read back differs: never "copied" then), a block that cannot carry an id (the page
  properties), Markdown source mode, the documentation notebook (no new block ids there:
  `PageController.allowNewBlockIds`, so its pages keep being updated).
* **Following a link** to a block or a heading (`((id))`, `[[Page#^id]]`, `[[Page#Heading]]`,
  Obsidian heading paths, also on the page already shown and on every click —
  `PageController.activateLink` scrolls there itself instead of asking for an unchanged route):
  `BlockEditor.revealRow` unfolds and leaves a zoom as needed, scrolls the block to the middle (a
  day of the journal stream: the stream scrolls it into view — `scroller.ensureVisible`, the day's
  own list never scrolls), selects it and flashes it (`BlockDelegate.flash()`: `Theme.mark` over the selection and under
  the text, ~1.2 s, created only while it shows, no height change); a heading or block that is
  nowhere is said ("Heading “…” is not on this page"; also from a query result). Toasts show
  their message as plain text (`BToast`): they quote note text — a copied link, a heading —, which
  must never be rendered as markup.
* **Add date** (`QuickDateMenu`, `qml/editor`): an open task (to do, in progress, waiting) without
  a 📅 due or ⏳ scheduled date (nor a Logseq `SCHEDULED:` / `DEADLINE:` line: the model's
  `taskDated` role, `editor::hasFilingDate`) has a calendar button at the end of its first line in
  the editor (`BlockDelegate`, `taskDateButton`) — shown while the mouse is over the block and while
  the block is edited (on touch only then); its room (28 px, 32 on touch) is kept at the end of the
  text while it is hidden, so the text never reflows when it shows. In Tasks such a row has a faint
  "Add date" chip (`TasksRow`, `taskAddDateChip`). Both open the menu: Today, Tomorrow, Next week
  (the first day of the next week) with their dates on the right (`BMenuItem.hint`), and "Pick a
  date…" (the date dialog on tomorrow, with a time; `EditorDialogs.pickDate` in the editor,
  `pickNewDateWith` in Tasks). The date is written as the task's `📅` at the end of its first line,
  before a block id (`markdown::setBlockToken`: only that line's bytes): one undo step in the
  editor (`PageController.setDateToken`; the edited block stays edited), "Due date set: …" with
  Undo in Tasks (`PlanningActions.setTaskDate`, `PlanningTokenMenu.openAddDate`). Done and
  cancelled tasks and read-only pages show neither. On a dated task (`BlockDelegate.addable`) the
  editor's button is its "+" ("Add a reminder or an alarm"): the date dialog adding a reminder
  (`BlockEditor.addDateTo`, `EditorDialogs.addDateTo`: "+" on, Reminder chosen; a 📅 due date
  instead when the task has only a ⏳ one), written at the end of the first line as the reminder
  dialog adds one, the dates kept.
* **Date, reminder or alarm** (the date dialog, `EditorDialogs`): the dialog of a 📅 ⏳ 🛫 date —
  "Pick a date…" above, "Due date…" / "Scheduled…" (commands, the touch toolbar), a chip's
  "Change date…" in the editor, a query result, an embed, a table cell, Tasks and the calendar —
  has a choice above the calendar (`BSegmented`, `dateDialogKind`): the date's own kind (its
  title and icon), Reminder, Alarm. Reminder and Alarm retitle the dialog, require a time (the
  notebook's default reminder time when the date had none) and hide "Remove"; Save writes a `⏰` /
  `⏰!` instead of the date — in the date token's place, the block's 🔁 rule kept, one undo step
  (`PageController.setReminderFor`; a chip: the token action "reminder", which turns any date
  token into a reminder, `markdown::applyTokenAction`) — or, for Tasks' "Add date", appended to
  the task's first line next to a reminder it has (`PlanningActions.addTaskReminder`, "Reminder
  set for …" / "Alarm set for …" with Undo). A page property's date, a journal link and a
  `[[day]]` link have no such choice.
  **A click or tap on a date chip** (📅 ⏳ 🛫 ➕ ✅ ❌, a `date:` link) opens this dialog on that
  token, as its menu's "Change date…" — in the editor's blocks (`BlockEditor.openDateDialogAt`,
  also a screen reader's press), query results and embeds (`openTokenDialogIn`: the change goes to
  the block's own file), table cells (`openCellTokenDialog`) and Tasks (a row's 📅 ⏳ 🛫 chip:
  `TasksRow.tokenDialogRequested`, `PlanningTokenMenu.openTokenDialog`; elsewhere the row opens
  the note); not in reading mode, where the click does what it did. **"+"** beside the choice
  (`dateDialogAdd`, shown on a 📅 ⏳ 🛫 date the block has) adds a separate one, the date it is on
  kept: the dialog says "Separately: … stays", hides "Remove", saves with "Add"; turned on over
  the date's own kind it chooses Reminder — a task has one date of each kind, so that kind while
  adding shows a hint (`dateDialogTaken`) and no "Add". The reminder or alarm is written right
  after the date token (the token action "addReminder", `markdown::applyTokenAction`; one undo
  step, in another file "Reminder added" with Undo).
* **Task status menu** (`TaskStatusMenu`, `qml/editor`): To do, In progress, Done,
  Waiting, Cancelled, No status, the current one checked. It opens with a right-click (mouse,
  touchpad, stylus) or a long press on any task checkbox (`TaskBox`) — in the editor, query
  results, embedded pages and blocks, linked references and tag pages, the calendar's day panel
  and agenda, Tasks (whose row menu adds "Open") —, with Ctrl+Shift+Enter in the editor and by
  holding the touch toolbar's Task button. A click or tap on the checkbox still toggles (a long
  press never does). The editor's menu (`BlockEditor.openTaskStatusMenu`) follows its block while
  open and gives the keyboard back to the edited block; elsewhere the choice goes through the
  view's model to the task's own file (`QueryResultModel.setTaskStatus`,
  `PageController.setTaskStatusAt`, `GroupedRefsModel.setTaskStatus`,
  `PlanningActions.setTaskStatus`; "" = no status, `markdown::removeTaskMarker`); Tasks and the
  calendar show "Status changed" with Undo (the dates, reminders, rules and priorities of those
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
* **Tasks: dependencies, subtasks and time** (docs/SYNTAX.md §5). The task status menu of the
  editor and of Tasks (`showTaskTools`) ends with **Start timer** / **Stop
  timer** and **Add dependency…**; the palette has "Start or stop task timer" (`task.clock`, the task
  at the cursor), "Add task dependency…" (`task.addDependency`) and "Stop task timer"
  (`task.stopClocks`, enabled while one runs, its subtitle the running task). Starting a timer
  stops the running one, wherever it is (`stopTaskClocksElsewhere()`, `taskhelpers.h`: other
  files written through the host, the editor's own page in the same undo step —
  `PageController.toggleTaskClock`; Tasks: `PlanningActions.toggleClock` with Undo in the toast).
  "Add dependency…" opens `TaskDependencyDialog` (`qml/editor`): a search over the notebook's
  open tasks (`notebook::dependencyCandidates`, diacritics ignored, the ones it already waits for
  left out); the chosen task gets a `🆔` when it has none and this one a `⛔ id`
  (`PageController.addTaskDependency`, `PlanningActions.addDependency`). The renderer
  (`RenderContext::taskDependency`, from `EditorHost::taskIndex()`) shows each `⛔ id` as a chip
  with the text of the task it waits for — red while that task is open, grey once done, "⛔ id?"
  for a missing id — and a click on it (`taskdep:<id>`) opens that task (`activateLink`; Tasks:
  `PlanningActions.openLink`). After a task's text the editor and query results show
  `markdown::taskChipsHtml()`: its subtask progress "3/5" (a parent row is rendered again when
  its subtasks change: `RowState::htmlProgress`) and its time "⏱ 1 h 05 min + from 10:00" (static:
  a running clock names its start). Tasks rows have chips of their own: "Waiting for: …" (a click
  opens the blocking task), "Dependency cycle" / "Missing dependency", "Blocks n tasks", "3/5" and
  the time. "Time spent" in Tasks' filter bar opens `TasksTimeDialog`: the listed tasks' time
  (the filters apply) today / this week / this month / everything, per day, page, tag or task
  (`TaskListModel.timeSummary`, `index/tasktime.h`); a task row opens the task. Settings →
  Editor: **Log task status changes** (`AppSettings.taskStatusHistory`, default off, pushed
  into the Workspace: `EditorHost::taskStatusHistory()`) logs every status change in the task's
  `:LOGBOOK:` and makes "In progress" start and stop its timer, in the editor (`logTaskStatus`, the
  same undo step), Tasks, the calendar and query results.
* A day without a note (journal stream or a single day) starts from the journal template; the
  note is created by the first change (`PageController.startNewPage`), never by looking at it.
* Special block delegates: code (monospace), `calc` (input lines with right-aligned results, in
  the note's font and size — results with tabular figures — on the sunken well with its
  "Calculation" caption; the 1-px frame shows only while editing),
  table (grid editor with formula results; a table wider than the page scrolls sideways — a
  finger, Shift+wheel, a touchpad, and a horizontal `BScrollBar` along its last row's bottom edge
  (`tableScrollBar`; always drawn with a mouse, so the table says it goes on) to drag; a cell's `<br>` (`<br/>`, `<br />`: GFM's line break,
  `tableCellLines()`) breaks its line — a header's lines one under the other, the header row as
  tall as its most (`headerLines`, at most three), a body cell's one after another on its one
  line, the columns as wide as their widest line; edits rewrite the Markdown table; a cell's block menu —
  a right-click on it, also while it is edited, or the cell toolbar's More — has **Row** and
  **Column**: insert (the new cell or header opens for typing), move, delete, the column's
  alignment, each one undo step, the only column and the header row not deletable; in a cell
  Ctrl+Enter inserts a row below and Alt+arrows move its row or column, the cell moving with it;
  **keys between cells** (a cell being edited): Tab / Shift+Tab the next / previous cell (Tab in the
  last cell adds a row), Enter the cell below (in the last row it adds a row), the arrows the
  neighbouring cell — Up / Down always (the header and the last row stop them: nothing is added), Left / Right
  when the caret is at the text's edge and nothing is selected (the caret then comes to the other
  cell's end / start, Up / Down keep its column in the text), Home / End the field's own; Esc ends
  the editing with the table selected. A **rectangle of cells** is selected with Shift+arrows (also
  Shift+Left / Right at the text's edge) and Shift+click, from the cell being edited — it is
  committed — or the selection's anchor: the cells are tinted (`Theme.accent`, the cursor cell
  outlined) and the keys go to `TableBlockView.rangeKeys` — arrows move the cursor cell (Home / End the row's
  ends, Ctrl+Home / Ctrl+End the table's), Shift+arrows grow it, Tab / Shift+Tab walk the cells (Tab at the
  last one adds a row), Enter / F2 edit the cursor cell and a typed character edits it with that text
  in place of the cell's, Delete / Backspace empty the cells, Ctrl+C / Ctrl+X copy / cut them as TSV
  (tabs between columns, lines between rows, a field with a tab, quote or line break quoted as
  spreadsheets do; formulas as their results, `<br>` a line break), Ctrl+A selects every cell, Ctrl+Z /
  Ctrl+Y undo and redo with the selection kept, Esc ends it (the table selected); a right-click on a selected cell
  has **Copy cells**, **Copy cells as Markdown** (a table of its own: the columns' header, the rows,
  alignments), **Cut cells**, **Clear cells** on the selection as it was when the menu opened. **Ctrl+V** —
  in a cell being edited when the clipboard holds several cells, else with cells selected — pastes a
  Markdown table, TSV, CSV (the separator detected) or lines with the top-left cell at the selection's
  (`io::parsePastedCells()`; `=` at the start of a TSV/CSV value is escaped as in the CSV import):
  cells below the last row add rows (at most 2000), columns past the last one are left out (a toast says
  how many), one value over a selection fills it, the pasted cells are then selected;
  each copy-cut-clear-paste is one undo step and a pure edit of the table text
  (`PageController.copyTableCells` / `clearTableCells` / `pasteTableCells`, `io/tablecells.h`: line endings and
  untouched text are kept). The selection is a mouse and keyboard one: touch UIs have no range
  selection (a long press selects the table);
  formulas keep naming the same cells, docs/SYNTAX.md "Table formulas"; a block menu not opened
  from a cell does not have them; **Column** also has Sort ascending / descending —
  `PageController.sortTable`, one undo step, the rows rewritten in the file line by line, summary
  rows in place, a toast when the rows are in that order already — and "Filter by this
  column…"; any table's block menu has **Filter rows** / **Hide filters**, **Copy as
  CSV** and **Save as CSV…** (`EditorDialogs.saveTableCsv`: the save `FilePicker`, the browser's
  download), docs/SYNTAX.md "Sorting and filtering tables", "CSV". Filtering shows a row of
  `BTextField`s under the header — one per column, the menu's column focused, Escape in an empty
  field closes the row (the page drawn by Atrament draws the same fields, its cell selection and
  the touch toolbar's cell tools the same way: docs/ATRAMENT.md M2 step 5, tables) — and hides the rows that do not match (`BlockListModel.setTableFilter`:
  view state of the block, nothing written, kept while the page is open; the edited row stays
  shown); a filtered column's header has a funnel, the line under the grid counts the rows shown
  next to "Clear filters". `/Table from CSV…` picks a file (`EditorDialogs.pickCsvTable`, the
  browser's picker: `PageController.pickCsvInBrowser`) and makes it a table in place of the empty
  block, a toast when rows or columns were left out), `query` (live
  results with checkable tasks; with `view:` a `DatabaseView` in place of the list — and a
  ```` ```base ```` block the same: "Database views"), `dataview` (Obsidian Dataview queries,
  docs/SYNTAX.md §10 "Dataview queries": the same `QueryBlockView` titled "Dataview", no filter chips —
  `QueryResultModel.language: "dataview"` over `QueryHost::runDataview` —; a `CALENDAR` query a
  `DatabaseView` month instead ("Database views": "Dataview calendar"); LIST rows rendered from
  their Markdown, TABLE rows as cells side by side under the column headers — equal columns, each
  cell up to 4 lines, the row as tall as its tallest cell; a link in a cell follows, a tap elsewhere
  opens the row's page —, TASK results grouped by page with checkable tasks), math (`math`/`latex`/`tex` fences, the formula rendered from
  the LaTeX source, also as a list item of an outline or a journal; a formula between two `$$`
  lines stays one text block whatever its lines start with; a block an edit leaves with a `$$`
  line and no closing one — a deleted `$$`, Enter inside the formula — gets a blank line after
  it when a later `$$` line would close it, so the items between never become formula text when
  the page is read again), Mermaid (`mermaid` fences, docs/SYNTAX.md §11 "Mermaid diagrams":
  `MermaidBlockView` — the diagram drawn by `MermaidView`, a `QQuickPaintedItem` of
  `src/app/editor/mermaidview.h` painting the core `diagram::Scene` with QPainter, text measured
  in the UI font, colours from Theme (`fg`, `fgMuted`, `bgSunken`, `border`, `accent`, `dark`:
  `diagram::themedPalette`) — on the sunken panel, centred, scaled down to the column to 70 % at
  most and then scrolled sideways (a horizontal Flickable, as a wide table); the item paints at
  most 4096 px on either side (`MermaidView::MaxPaintExtent`: a taller or wider diagram is scaled
  down further, so its texture fits every GPU and phone); laid-out sources are
  kept in a cache of 64 shared by all views, so delegates made again while scrolling lay nothing
  out and a change of theme only repaints. A click (Shift/Ctrl+click: block selection; a finger
  held: the block's selection) edits the source in place — CodeBlockView's monospace editor with
  the diagram under it, drawn again at every keystroke; a source that cannot be drawn shows the
  source with the reason under it (`MermaidView.message`, translated: a mistake with its line in
  `dangerText`, a type Brulion does not draw yet in `fgMuted`). The same in the outline and the
  document mode; an embedded page or block shows the source as code), charts (`chart` fences,
  docs/SYNTAX.md §9 "Charts": `ChartBlockView` — `BlockListModel`'s `chartData` holds the numbers
  of the table, calc block or query the block names (core `chart::resolveData`, computed again on
  every change of the page; a query chart when its text or the index changes), `ChartView`
  (`src/app/editor/chartview.h`, a `QQuickPaintedItem`) lays them out at the column's width in the
  UI font (`chart::layoutChart`: bars, lines, a pie, axes, legend) and paints the `diagram::Scene`
  with MermaidView's painter in Theme's colours, light and dark; its `description` — the type,
  title and every value — is the accessible description; editing as for Mermaid, the chart under
  the source following every keystroke; a chart that cannot be drawn shows the source with the
  reason in `dangerText`; `/Chart` inserts `type: bar` / `table: above` (in Polish notes `typ: słupkowy` / `tabela: powyżej`)), extension views (`view` fences, docs/SYNTAX.md §13 "View blocks":
  `ViewBlockView` — a frame on the page's paper with the view's title and a puzzle icon, the view
  drawn by `ShellExtensionView { embedded: true }` over an `ExtensionView` given the block's source,
  the page's path and `live: block.shown && controller.active`, drawn at once when the block is
  made so the row has its height in its first frame; its controls take their own taps, a tap
  elsewhere (Shift / Ctrl+click: block selection; a finger held: the block's selection) edits the
  source in place with the view under it; a source that names no view, a view that cannot be drawn
  or an extension that does not run here show why, the last with a button that opens
  `ShellExtensionApproval` (its code to read and run) when it waits for approval, else
  `ShellMacroDialog`; the slash menu's "View: …" rows — one per labelled view,
  `EditorHost::embeddableViews` — insert one), callout, embeds/transclusions
  (`![[…]]`, `!((…))`), images, headings, horizontal rule, properties / frontmatter
  ("Properties" panel: typed fields, "Page properties" below).
* **Macros of extensions** (`{{ext:silownia/rekord Przysiad}}`, docs/SYNTAX.md §13 "Macros"): the
  renderer asks `EditorHost::macroView` (Workspace → `WorkspaceExtensions::macroView`: never runs
  Luau, a cached value or "…") and shows the value's pieces — text in a tone, bold, a page link,
  code — escaped in the text; a placeholder of an extension waiting for approval, off, unknown or
  failed is a chip that is a `macro:<from>-<to>` link (its range in the rendered body): a click
  of an extension waiting for approval opens `ShellExtensionApproval` (the code read, then "Run
  this version": the approval of the version read, as the page's "Read and run"), any other
  `ShellMacroDialog` (switch it on, open its error, none offers it).
  A value is recorded as the row's `RenderRead::Macro`: on `EditorHost::macrosChanged` (values
  computed, extensions approved or loaded again; coalesced, 30 ms) `BlockListModel::followMacros()`
  asks again only for those rows and renders again the ones whose macro shows something else; an
  editor not shown follows when shown again (`PageController.active`). Typing `{{` completes the
  extensions' macros (`SuggestionModel` mode "macros", `EditorHost::suggestMacros`): their
  `ext/name`, description and arguments; the completion writes `ext:<ext>/<name> <example>` and
  closes the braces when they are not there, the caret before them.
* **Luau code** (```` ```luau ````, ```` ```lua ````: an extension page's code) is shown in Luau's
  colours, read and edited — `CodeBlockView` gives its `InlineEditor` the block's language
  (`highlightLanguage`), whose `MarkdownHighlighter` (`language`) colours keywords, constants, the
  library and `b`, function names, numbers, strings and comments (core `syntax/luau.h`; a long
  string or comment carried across lines in the block's state); the source mode colours such a
  fence's lines the same way. The colours are the editor palette's `keyword`, `constant`,
  `builtin`, `function`, `number`, `string` (`BlockEditor.highlighterPalette`), light and dark.
* **Line numbers in code blocks:** Settings → Editor → "Line numbers in code blocks"
  (`AppSettings.codeLineNumbers`, device-local, off; `b.app.settings()` key `codeLineNumbers`)
  numbers each line of a code block's text from 1 in a column on its left (`CodeBlockView`: the
  code font and line height, `fgFaint`, right-aligned, as wide as the longest number), read and
  edited alike. A wrapped line has its number once, on its first visual line
  (`BlockViews.codeLineNumbers`: one line of the column per visual line of the editor's laid-out
  document, followed again when the text, its wrapping or the width change). The numbers are not
  part of the text: the file, a copy and the export have none. Mermaid, chart and view sources are
  not numbered. A note's own `line-numbers` option wins over the setting ("Note options" below).
* Chips inside rendered text: 📅 due (red when overdue), ⏳ scheduled, ⏰ reminders (amber,
  bell for alarms), 🔁 recurrence in words of the UI language. Clicking a ⏰ chip opens the reminder dialog
  (edit / snooze / delete), a date chip the date dialog on that date (with "+": "Date, reminder or
  alarm" below); a click on a 🔁 chip or a priority edits the text (an I-beam cursor). While its block is edited a ⏰ / ⏰! token stays a chip
  with the rendered chip's words (`markdown::reminderChipLabel()`: "⏰ dziś 18:00", "🔔 jutro 06:30",
  relative to `PageController.todayDate` in the UI language) on a pill of the chip's colours, while
  the edited text and the file keep the token byte for byte (`MarkdownHighlighter.reminderChips` /
  `chipShapes`, `label` drawn over the pill by `InlineEditor`): the token's characters are laid out
  invisible, the first one as wide as the words and the others with no width, so the chip never
  wraps inside (a line too short moves it whole to the next). A click or tap on it (a pointing
  hand) opens the same reminder dialog on that token (`BlockEditor.editReminderInText`, the edit
  text's range plus `PageController.editOffsetAt`) without moving the caret, and the block is edited
  again when the dialog closes; a right-click or a long press (a finger) opens its `DateChipMenu`
  as on the rendered chip — An hour later, Tomorrow at this time, A week later, Alarm, Repeat,
  Change date and time…, Delete reminder (`BlockEditor.openReminderMenuInText`,
  `PageController.editTextChip`; `applyTokenAction` keeps a caret after the token where it was in
  the words) —, the block stays edited, the keyboard back in it when the menu closes, one undo step
  per choice. To the keys a chip is one unit (`InlineEditor.chipKey` / `chipSnap`, the
  highlighter's `chipEndingAt` / `chipStartingAt` / `chipFromAround` / `chipToAround`): Left /
  Right step over it, Shift+Left / Shift+Right select it whole, Backspace after it and Delete
  before it remove it whole (one step of the block's typing), a caret put inside it (Up / Down, a
  click beside it, the controller) goes to its end, where it is drawn, and a selection's moving end
  takes it whole; Vim's command modes and a word being composed leave it alone. Its text is edited
  by hand in source mode, which shows tokens as written (ctest `tst_editor_qml_datechips`,
  `_touch`; `tst_editor_highlighter`). "today"/"tomorrow" and overdue colours of open pages move on at midnight
  (`DayClock`). Links to daily notes read as dates (`[[2026-10-13]]` → "Tue, Oct 13").
* **Token and link menus.** A right-click (mouse, touchpad, stylus) or a long press (a finger,
  outside the selection mode) on a token or a link of rendered text opens its menu instead of the
  block menu (BlockText's own handlers: the token's menu, else the link's, else the block menu as
  the list's handler opens it, `BlockEditor.openBlockMenuAt`; a long press elsewhere selects the
  block). `DateChipMenu` (`qml/editor`) by the token's kind:
  * 📅 ⏳ 🛫 ➕ ✅ ❌: Change date… (the date dialog on that token, `EditorDialogs.pickDateAt`), Today,
    Tomorrow, A day later, A week later, Remove date;
  * ⏰ / 🔔: Change date and time… (the reminder dialog), An hour later, Tomorrow at this time,
    A week later, Alarm (checkable), the submenus **Repeat** (Change rule…, the quick rules
    of the recurrence dialog — Daily, Weekdays, Weekly, Every 2 weeks, Monthly,
    Yearly —, Don't repeat: the block's 🔁, which makes the reminder recur, SYNTAX §6) and, on a
    task, **Priority** (Highest … Lowest, No priority: the task's priority, added to its
    first line when it has none) — their Don't repeat and No priority are choices like the
    others (no red, no ×: "Design system in code"), shown while there is a rule or a priority —,
    Delete reminder;
  * 🔁: Change rule… (the recurrence dialog on that rule, `EditorDialogs.pickRecurrenceAt`), the
    quick rules (the current one checked), From the completion date (`when done`, checkable), Don't repeat
    (a choice like the rules, as in a reminder's Repeat: no red, no trash)
    — the rule is written canonical, text after it ("🔁 co tydzień kupić mleko") stays;
  * a priority (🔺 ⏫ 🔼 🔽 ⏬; Logseq `[#A]` `[#B]` `[#C]` with three levels, written as Logseq):
    Highest … Lowest, the current one checked, No priority (a choice like the levels, as in a
    reminder's Priority);
  * a link to a day of the journal (`[[2026-10-20]]`, also `[[20.10.2026|wtorek]]`): Open day,
    Open in side panel (not on narrow windows), Copy link, Change date… (a date without a
    time), Today, Tomorrow, A day later, A week later — only the date part of the target changes,
    in the format it was written in (an alias, a folder, ".md" stay).

  `LinkMenu` (`qml/editor`) on a `[[page]]` (also a Markdown link to a note): Open ("Create
  page" for one not written yet — created by its first content, as a click does), Open in
  side panel, Copy link (`[[Page#Heading]]` by the page's shortest link target — it works
  pasted on any page —, as written for a page that does not exist; `PageController.linkMenu`, in Tasks and the calendar
  `PlanningActions.linkText` / `linkMissing`); on a path part of `[[Folder/Page]]` (a `part:`
  link): Open (its page), **Show folder** (a folder: its All pages) or Create page (nothing
  there yet), Open in side panel, Copy link (`[[Page]]`) or **Copy folder path**
  (`Projekty/Dom/`, what a `path:` filter takes — quoted when not one token,
  `"Zażółć gęślą/"`; `LinkMenu.folder`, `PlanningActions.linkPartTarget`); on a
  `#tag`: Open tag page, Open in side panel, Tag overview, Tasks with this tag, Copy tag
  (`#tag`, `#[[tag with spaces]]`); on a `((block))`: Open, Open in side panel, Copy link;
  on a web address: Open in browser, Copy address. Every copy says "Copied …" or that the
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
  meanwhile — `EditorHost::writeIfUnchanged` — with a toast "Date changed" / "Repeat
  changed" / "Priority changed" and **Undo**, `undoTokenEdit`, which puts the file back
  only while it still holds that edit; a token that is not there any more: "The note changed in
  the meantime; nothing was changed."), **Tasks** (the chips of a row: due, scheduled, start, the reminder, the rule, the
  priority) and **the calendar** (an entry of the day panel, the agenda, the overdue list, the
  week grid) — `PlanningTokenMenu` (`qml/planning`) with `PlanningActions.editToken` (the task by
  its text near the index's line, like the status menus) and the views' "Undo" toasts
  (`PlanningActions.undo`); `PlanningActions.tokenMenu` reads the block's rule and priority from
  the file (the rule at the start of a 🔁 token with words after it, a Logseq `[#A]`) and opens no
  menu for a value that is no token there (a Logseq `SCHEDULED:` / `DEADLINE:` line, a `.+1w`
  repeater). A calendar entry that is another date of a recurring series (`CalendarModel` entry
  `occurrence`; `spec` is the token as written) offers "Change the whole series…" instead of moving one
  date, and a reminder's time that has rung offers An hour later / Tomorrow at this time, a snooze of
  that one time (`PlanningActions.snoozeReminder`: a one-off child reminder, as the notification's
  "Snooze"); a time still to come offers no snooze (it would ring as well). The 🔁 rule changes only
  in its own text (`markdown::setBlockRecurrence`: words after it stay; the same rule in another
  phrasing — the reminder dialog with only the time changed — stays as written). The tokens are named across the copies of a block by
  `markdown::TokenRef` (kind, value, which of the equal ones; markdown/tokenedit.h); the edit is
  `markdown::editBlockToken` — only the token's bytes of its line change (CRLF, indentation and
  every other line stay). The rendered text carries the links (`RenderContext::dateLinks`:
  `date:FROM-TO`, `recur:FROM-TO` — the range ends with the rule —, `prio:FROM-TO`; `dayLinks`, the
  editor's own blocks only: `day:FROM-TO` for links to days, opened as their `page:` link; ranges
  in the block's body, `reminder:` always). Closed tasks keep their menus (a finished task's dates
  can be corrected).
* Source mode (Ctrl+E, More → Markdown source): one text area with the whole file and
  highlighting — also where any characters across several blocks are selected (the block editor
  selects whole blocks: "Selecting and copying"). A mouse selects by dragging; on touch UIs a
  finger scrolls it (a TextEdit that selects by dragging keeps the finger: the source could not be
  scrolled at all), a tap places
  the cursor and a long press selects the word under the finger (a TapHandler on an item above
  the TextEdit, which leaves a finger's taps alone too when it does not select by dragging; the
  system's selection handles take a selection on where the platform has them). Each edit reaches
  the controller alone (`SourceTextSync`: where `QTextDocument::contentsChange` says the text
  changed, the file's line endings kept by `sourceEditOf`), not the whole page per keystroke; once
  typing pauses and before a save the two texts are compared once (docs/PERFORMANCE.md "Source
  mode: one edit, not the page").
* Drawings (an SVG with an embedded scene, a `.excalidraw` scene, a drawing of Obsidian's Excalidraw
  plugin — `![[Szkic.excalidraw]]`, `![[Szkic.excalidraw.md]]`, a page embed `![[Plan]]` of a
  plugin file, not a transclusion then —, docs/SYNTAX.md §2 "Drawings"; the embeds' `drawing` and
  `path`, `EditorHost::isDrawing`, the size of a scene's preview `EditorHost::drawingSize`): shown
  at their own size (one scene unit a pixel, never enlarged as other SVGs are) with a pen in the
  corner; a click opens the drawing editor
  (`PageController.openDrawing`: the page is saved, route `drawing`), so does "Edit drawing" in
  the picture view's bar. "/Drawing", the touch toolbar's Drawing and the block menu's "Insert
  drawing" (`BlockEditor.insertDrawing` → `PageController.insertDrawing`: an empty drawing stored as
  `Drawing <yyyy-MM-dd HH.mm>.svg` — `Rysunek …` in a Polish UI —, embedded at the caret, one undo step) open the editor on a new
  one. Its picture is Brulion's own rendering in the app's theme (the image provider `drawing`,
  "Drawing editor" → Previews: the embed's `url` light, `darkUrl` dark, `EditorHost::drawingUrl`),
  so a drawing on "Like the app" turns dark with the app at once. A drawing saved again is shown again
  at once: its URL carries its version (`Workspace::drawingUrl` / `assetUrl` add
  `v=<mtime>-<size>`; Qt's pixmap cache keys by the URL) and the embeds of the page follow the
  file's change (`PageController` on `fileChanged` → `BlockListModel::attachmentChanged`).
* Files as links: "/File…" (`fileLink`, "Plik…" in Polish) opens the file picker (several files, any type,
  the browser's too) and writes a link for each at the caret (`PageController::insertFileLinks`):
  `[[raport.docx]]` by the shortest target (`EditorHost::linkTarget`), `[C#.pdf](assets/C%23.pdf)`
  when a wiki link cannot hold the name. A file of the notebook stays where it is; another one
  is copied to the attachments folder, or the same file there is reused
  (`EditorHost::importAttachment`). A click opens it as any link to that file: a note, a picture,
  the PDF viewer, otherwise read-only as text.
* Pictures: Ctrl+V pastes a copied picture as an attachment, files dropped on the page are
  stored in the attachments folder and embedded, "/Image…" and the toolbar pick files (the
  pictures: `paths::pictureExtensions()`, docs/SYNTAX.md §2). In a note (`EmbedView`) a picture
  sits on the page's own colour (`Theme.bg`: a transparent PNG's background is the page's, as
  beside it), without a frame; Settings → Editor → "Frame around pictures"
  (`AppSettings.pictureFrames`, device-local, off; `b.app.settings()` key `pictureFrames`) draws
  the hairline `borderSubtle` frame around each. A picture not shown yet (loading, from the
  internet waiting for "Show", a format the device cannot decode) has its frame either way: it
  marks the picture's place. A note's own `picture-frames` option wins over the setting ("Note
  options"). In a dark theme a picture lies on a light backing (`Theme.pictureBacking`, a dimmed
  paper) under its painted area — in the note, in `ContentPictureView` and in `WebPictureViewer` —
  so a transparent PNG or SVG with dark lines (a diagram, a scan) stays readable; an opaque photo
  covers it. Settings → Editor → "Light background under pictures in a dark theme"
  (`AppSettings.lightPictureBacking`, device-local, on; `b.app.settings()` key
  `lightPictureBacking`) turns it off; a note's own `picture-backing` option wins over it in the
  note and its `WebPictureViewer` ("Note options"). A drawing never gets it (its picture has a dark
  version).
  A picture is loaded only while its frame is in or near the window's view, decoded
  for the frame and not at the file's size ("Scrolling performance" → "Pictures hold their pixels
  only near the view"). Opening one (a click on the embed,
  a link to the file) shows the picture itself (`ContentPictureView`, loaded by `PageView` for a
  `pictureRoute` only): fitted into the view with its aspect ratio kept — fitting only shrinks a
  raster picture (a smaller one keeps its own pixels and gets no size toggle), an SVG fills the
  view —, decoded for the size it is shown at (`PictureDecode`; its own pixels at most 2048 px a
  side); when the fitted picture differs from its own pixels, a click on it or the "Zoom to
  actual size" button (in a view narrower than 600 px — a phone, or on the desktop between
  the sidebar and the side panel — a magnifier icon, its name in the tooltip, on touch that of a
  long press, and for screen readers: as text it left the file name no room) shows those pixels (then
  draggable), the button goes back. Under the
  fitted picture (it moves to the top then) the notes that embed or link it (`ContentBacklinks`);
  the side panel's Links tab lists them too. The bar shows the file name as plain text. Its
  "Show as text" / "Show picture" and Ctrl+E (the palette's "Markdown source", the top bar's
  More → Show as text / Show picture) switch between the picture and the file read as text —
  read-only, for text-like assets such as SVG (a binary picture is mojibake), at most its first
  200 KB (`Workspace.readTextStart` reads no more of the file — a photo is not read whole for
  it — and leaves out whole a character the limit would cut; a note above the text says how much
  of how much), read again when the file changes on disk while it is shown (sync, another
  program: `Workspace.fileChanged`); a mouse selects in it, a finger
  scrolls it (no selection by dragging on touch UIs: such a TextEdit keeps the finger). The editor never loads a picture
  (`page.path` stays ""): its bytes are not a note, and a character typed into them used to be
  saved over the picture. A format the device has no decoder for (`Workspace.canDecodePicture`,
  QImageReader's formats: WebP and AVIF need Qt's image plugins, missing on Android, in the
  browser and in the Nix package) says so ("Pictures in the WEBP format cannot be shown on this
  device.") instead of a load error — in its view and embedded in a note (`EmbedView`: the
  file's name and the same sentence in the picture's frame, nothing loaded; the embeds'
  `decodable` and `format` from `EditorHost::canDecodePicture`, one helper with the picture
  view: `canDecodePictureFormat`, picturesize.h). `route.open` on a picture: `text`, `links` | `outline` |
  `calendar`.
* PDFs (`![[wykład.pdf]]`, `![[wykład.pdf#page=3]]`): the page's thumbnail (`PdfEmbedTile`), a
  click opens the PDF viewer — "PDF viewer" below; without a renderer the file's chip, whose click
  opens a new browser tab / the system's viewer. Screen readers hear "PDF wykład.pdf, page 1 of 3",
  or for a tile that cannot show its page the reason ("PDF sejf.pdf: PDF protected with a password").
* Pictures from the internet (`![alt](https://…)`, docs/SYNTAX.md §2): the embeds' entry has
  `web`, `state` ("ask" | "loading" | "ready" | "failed"), `host`, `error` and, once ready, the
  cached file's `url`, `format` (the cached picture's as the host tells it, "PNG", "SVG" — not
  from the URL, a data: URL in the browser has no file name) and size (`EditorHost::webPicture`, the Workspace's
  `workspacepictures.cpp`: fetched into the device's cache, never by QML; an SVG is known by
  that `format`, its URL need not end with `.svg`). `EmbedView` gives a picture not shown yet its
  frame (320 × 180, higher when its text and buttons need it — a phone's narrow column, a nested
  block —, and as high while it loads) with the alt text (at most 2 lines), "Picture from <host>" on a
  line of its own (never elided away by the alt text), **Show** (`PageController.showWebPicture`;
  **Try again** with the reason after a failure) and **Save to notebook** side by side,
  one under the other when they do not fit, centred; a spinner while it loads. A picture fetched
  for the first time has no size until it is there: its row changes height when the fetch ends
  (with the setting below on, also while the journal only scrolls — "Scrolling performance",
  "Still there"); later the size comes from the cached file. A click (a tap) on the shown
  picture opens it on its own in `WebPictureViewer` (`EmbedView.webOpen` →
  `BlockEditor.openWebPictureViewer`), as a click on an attachment opens the picture view: a
  modal popup over the whole window with a bar — "<host> › <file>" (plain text), the size toggle
  (fitted ↔ its own pixels, dragged around; a click on the picture toggles too), **Save to
  notebook**, **⋯** (the menu below, without "View picture") and **Close**; Escape and Back
  (Android) close it. It draws the cached copy as the picture view draws an attachment (fitting
  only shrinks a raster picture, an SVG fills; decoded at most 2048 px a side) — no references,
  no text view. A right-click (its own `WithinBounds` handler: the block menu does not open as
  well) or long press opens `WebPictureMenu` — its first, disabled line "Picture from <host>" (BMenu
  shows no title), then View picture (the viewer), Save to notebook, Open in browser (the
  platform's handler), Copy picture address. "Save to notebook"
  (`PageController.downloadWebPictures`) fetches it when not cached — also while pictures are not
  shown —, stores the attachment and points every picture of that URL on the page at it, one undo
  step; a toast says how many were saved or which failed (a picture edited away meanwhile, or whose
  fetch ends while the page is shown as source, counts as failed, nothing is stored). Pictures in
  blocks that show no embeds (code, formulas, tables, HTML) are text: never fetched or rewritten. The palette's "Save pictures from the internet to notebook" (uiAction
  `saveWebPictures`; the page, or in the journal stream the day being written in — today until
  another day gets the caret, as the stream's other page commands) does that for all of them. "/Picture from
  the internet…" (`EditorDialogs.pickWebImage`) asks for the address (Insert only for an http(s) URL;
  otherwise "Enter an address starting with http:// or https://") with "Save to notebook"
  checked, every time it opens: `PageController.insertWebPicture` writes `![](url)` (between
  `<…>` when it has blanks or parentheses) at the caret, or as a block of its own after a block that
  is not text. Settings → Editor → "Show pictures from the internet without asking"
  (`AppSettings.webPicturesShown`, device-local, off) → `Workspace.webPicturesShown` (not for
  private hosts — those keep "ask" —, once a session per picture; turned off, the fetches it
  queued are cancelled and their rows ask again, "Show" / "Save to notebook" go on).
  Pasting (`PageController.pasteClipboard`, Ctrl+V and the text menu's Paste): a picture's address
  alone (a browser's "Copy image address": an http(s) URL without blanks whose path ends with a
  picture extension, `PageController.isPictureAddress` — by its form, nothing is fetched) goes in
  as the bare address it is, and a toast "Picture address pasted" offers **Save to notebook**:
  `PageController.urlToPicture` turns exactly that text into `![](url)` (only while it is still
  there — else "Not changed: the address was edited since") and stores the picture
  (`insertWebPicture` with `download`; in a quote or callout it is replaced where it is too).
  Offered only where the text reads it as a bare address of its own (a `LinkKind::Url` link of
  exactly the pasted range): pasted into a link (`![](…)`, `[x](…)`), an autolink `<…>`, inline
  code, a formula, a comment, a code block or a block that shows no pictures, nothing is offered. HTML of a web page (`htmlToMarkdown`) brings
  its pictures as `![alt](https://…)`; the ones new on the page (not this app's own copy, not
  `data:` pictures) are named in the result (`webPictures`), and a toast "Pasted %n picture(s) from
  the internet" offers **Save to notebook** for them (`downloadWebPictures` each; the result toast
  as usual). The same over selected blocks (`pasteOverSelection`). Nothing is fetched before the
  user asks. A paste that only the system's keyboard or the browser's own paste delivers
  (the text arrives as typed text) offers nothing.
* Mobile toolbar (`MobileEditToolbar`, touch UIs, docked on the keyboard — "On-screen keyboard";
  scrolls sideways): for a block Outdent / Indent, Move up / Move down, Task (a
  tap: the next status like Ctrl+Enter; held down: the task status menu, above the toolbar), Link
  to page (`[[`), Tag (`#`), Commands (`/`), Due date, Reminder, Insert picture, Drawing, Undo, Redo,
  Select (the edited block becomes the selection: the touch selection mode), More (the block
  menu of the edited block, above the toolbar: copying links to it, …), Hide
  keyboard; for a table cell Undo, Redo, Next cell, More (the table's block menu: the
  cell's editing ends, the table is selected), Hide keyboard; in source
  mode Link to page, Tag, Commands, Undo, Redo, Hide keyboard; in the touch selection
  mode the selection's tools (`SelectionToolbar`, "Selecting and copying").

### What an index change renders again

Every save (an autosave of any editor, another device's file, an import) changes the notebook's
index, and `EditorHost::indexChanged` reaches every live editor: the page, the side panel's page,
the days of the Journal. Each follows the index's change log (`VaultIndex::changesSince()` since
the version its rendering is current with, `BlockListModel::followIndex()`) and renders again only
what read a changed note — an edit of an unrelated note changes no row, no `dataChanged` reaches
the delegates:

* While rendering, a row records what it read of other notes through the host (`RenderRead`: a
  linked page's icon, the text of a `((id))` or `[[Page#^id]]` block, the task a `⛔ id` names, the
  value of an inline Dataview query — `QueryHost::evaluateDataview`, asked again after every change) in
  its html, its page properties, its table cells and its transclusions, with what it got. After a
  change a read is asked again — an icon only when its note changed, block texts and tasks always
  (a block may have moved) — and the row renders again only when the answer differs.
* Transclusions (`![[Page]]`, `![[Page#Heading]]`, `!((id))`) are built again when a note they
  show or looked for changed (`transclusionSources`, also a page drawing that may become a page
  again) or a block they found by its id reads otherwise; attachment embeds when the note drawing
  they show changed (a changed attachment file: `PageController::onFileChanged`).
* Tables are computed again only when a cell read something that changed; charts of a query on
  every change (their results may differ); a table's, a calc block's or a chart's own numbers and
  inline calculations follow the page's text, not the index. An inline Dataview query
  (`` `= this.file.name` ``, `RenderContext::inlineQuery` — asked only when the calculator has no
  value) follows the index: its RenderRead is asked again after every change. Its value is a
  neutral chip like a calculation's (links and tags rendered as in the text, a reminder's chip
  without its link), an error a muted chip with "⚠" and the message.
* The page's own text is followed by the editor's own path (`refresh()`: rows with `((id))` /
  `[[…#^id]]` and embeds of the page itself) — its save changes nothing more here.
* A change of names (a note, an attachment or an alias added, removed or renamed: links, tags and
  `page:` targets may resolve differently anywhere), a change log that no longer reaches back,
  another index (another notebook) or another vault configuration invalidates everything as
  before (`invalidateRendering()`, also for another palette, language, day or host): every row
  renders again when it is asked for.

While a notebook loads, every merge of the index loader adds names, so each one would make
every visible editor render all its rows again and the lists compute again: `Workspace` passes
the loader's merges on as `EditorHost::indexChanged` at most every 250 ms (the first at once, the
rest together, all of them before `ready`), any other change of the index — an edit, a file
changed on disk, `finishIndexing()` — at once (with the merges held back so far), so a save's
`indexChanged` still comes before its `revisionChanged`.

`PageController.active` (QML: `active: root.visible` in PageView, `day.visible` in
ContentJournalDay): an editor that is not shown — the days of the hidden Journal, the page of a
hidden view or a closed side panel — only marks its rendering stale when the index changes, and
follows all the changes at once when it is shown again (`setActive(true)`, before its next frame).
Source mode is treated the same way for the rows: the page shows its text, and the list of rows
behind it is hidden, so an index change there (each autosave of the source among them) renders no
row. Leaving source mode follows everything that changed meanwhile, once
(`PageController::followStaleRows()`, after the source's text is applied). The cover still follows
the index in source mode.
The autocomplete popup's model computes nothing for a closed popup (`SuggestionModel.clear()` in
`closeSuggestions()`: a `[[` once completed ranked every page name after every save), and the
side panel's outline ignores `dataChanged` of roles it does not show (html rendered again).

### Coloured highlights

Next to `==highlight==` a highlight can have a colour (docs/SYNTAX.md §11 "Coloured
highlights": Highlightr's `<mark style="background: #FF5582A6;">`, read by Obsidian, GitHub,
Pandoc). Rendered text shows it as a tinted box like `==…==` (`RichTextPills`), in one of nine
translucent palette colours that read on light and dark paper — never a colour or style of the
note's own; the edited text tints the highlighted words and shows the tags faint.

**"Highlight"**: the edited text's right-click menu ("Highlight…") and the touch toolbar's highlighter
button open the colours (`BlockEditor.openHighlightMenu`, `highlightMenu`): Default (`==…==`),
Red, Orange, Yellow, Green, Cyan, Blue, Purple, Pink, Grey — each with
its swatch — and "Remove highlight". A colour highlights the selection (blanks at its ends left out;
highlights inside it merged into the new one), or recolours the highlight the caret or selection is
in; "Remove highlight" takes the highlight around the caret away (`PageController.setHighlight`,
`markdown::setHighlight`; one undo step, the highlighted text stays selected). A selection over
several lines, or one cutting a highlight's marker, is refused with a toast. Tests:
`tst_markdown_highlight`, `tst_editor_look` (the controller's edit and undo).

### Input rules

Text an extension replaces as it is typed (`b.app.inputRule`, docs/EXTENSIONS.md "Input rules":
`->` → `→`). The shell's `Workspace` keeps the rules of the extensions running as a plain list
(`EditorHost::inputRules()`, made again when they change the app), so a keystroke never waits for
Luau. `PageController::setEditText()` — the inline editor's every keystroke — pushes the typing
step, then asks `inputRuleHit()` (`src/app/editor/inputrules.h`, pure) about the text before the
caret: only an insertion that ends at the caret and is no longer than the match (a paste is
not typed), in a paragraph, list item or heading being edited, after a keystroke the controller did
not normalise. A hit is a step of its own (`applyInputRule()`): the inline editor gets the new
text and caret (`editTextReset`), one undo brings back the typed text with the caret after it, and
what is typed next starts another step. Nothing fires in code — a code span (also one still open
before the caret), a fenced block or `$$` lines inside the text, a code block —, an inline
formula (also one a `$` typed at the caret would close), a `%%` comment, a wiki link or embed
not yet closed, a Markdown link's destination or a URL (`inputRuleProtected()`; a code span or a
comment ends where a line starts a list item, a heading or a quote, as in Markdown). Source mode's
TextEdit, with its own undo stack, has `SourceInputRules` (`src/app/editor/sourceinputrules.h`):
typing is a change of the text while a key's event or an input method's commit is delivered to the
TextEdit (an edit made by code — find and replace, a spelling correction, the toolbar's inserts,
Vim's commands — fires nothing); the match is found on the caret's line alone
(`inputRuleMatch()`), and only then the whole text is read for the protection — the same, and the
frontmatter (`sourceInputRuleProtected()`) —, so a keystroke costs its line. The replacement comes
after the key's event, if the text is still what the rule saw, as one edit block of the TextEdit's
undo that ends with the typed text's removal (QTextDocument merges what is typed next into a block
that ends with an insertion): one undo brings the typed text back, what is typed after it is a
step of its own. Tests: `tst_editor_inputrules`, `tst_editor_sourceinputrules`, the playground's
"input rules" steps (`tst_editor_qml`: typed in both modes).

### Styles of blocks by tag

A block carrying a tag an extension styled (`b.app.tagStyle`, docs/EXTENSIONS.md "Styles of
blocks by tag") is drawn on a tinted band from its own level to the text column's end with a 3 px
bar at its left edge (`BlockDelegate`'s `tagTint` Loader, under the selection band and the
flash; made only for such blocks). `EditorHost::tagStyles()` holds the styles by the tag in lower
case; `BlockListModel`'s roles `tagTint` (the tint, "" none) and `tagTintMode` (1 the background,
2 the bar, 3 both; read through `BlockRowData`) take the first tag of the block's own text with a
style or a parent with one (`#projekt/dom` by `projekt`); tags in code are none. The roles are
among the text's roles (typing a tag tints at once) and `EditorHost::tagStylesChanged` tells every
row again. The theme's tints are its pairs (`Theme.danger` / `Theme.dangerBg` …, `neutral`:
`Theme.borderStrong` / `Theme.bgSunken`); a `#rrggbb` is the bar as it is and the background at
13 % (light) or 20 % (dark) opacity. `PageItem` hosts a tinted row as its `BlockDelegate`, and
`JournalDayItem` leaves a day with one to its editor's rows (both behind their environment
switches): the tint is drawn in one place (`tst_editor_pageitem` `hostsTintedRows`,
`tst_editor_journalday` `tintedBlockIsTheEditors`). Which style a block gets is core's `markdown::tagStyleOf`
(`markdown/tagstyle.h`), the same for the exports: `io::ExportInput::tagStyles` (the export
dialog and a page's own export give the workspace's), drawn by the HTML renderer as the element's
style — the tint at 14 % as the background, a 3 px bar, the theme's names from the export's own
palette, nothing that is not a colour (`tintStyle()`) — and kept as the block's background by the
PDF and DOCX documents made from that HTML (DOCX: over white, it has no transparency). Tests:
`tst_markdown_tagstyle`, `tst_io_export` (`htmlTagTints`), `tst_io_paper` (`tagTintsOnPaper`),
`tst_appio_export` (`tagTints`: an extension's style to the files). Tests: `tst_editor_inputrules` (the roles), the playground's
self-test (`--editor-script selftest`: `checkTagStyles`).

### Selecting and copying

One block is edited at a time (its `InlineEditor` selects characters inside it); a selection that
crosses blocks is a **block selection** (Logseq / Notion): whole blocks, each with its subtree —
folded children too —, the same in both note modes (a document's paragraphs, headings and list
items), one run of blocks or blocks picked one by one with gaps between them (bullets 1, 3 and
5). Any characters across blocks — part of one paragraph to part of the next — are selected in
the Markdown source (Ctrl+E, More → Markdown source).

* **Model** (`PageController`, docs/ARCHITECTURE.md): two uids, the anchor (`selectionAnchorRow`)
  and the focus end (`selectedRow`); the selection is the visible rows between them with every
  subtree — a set of uids in `BlockListModel` (`isSelected`, `selectionEdge`: `single` | `first` |
  `middle` | `last` over the visible rows, so a selection with gaps is a band for each run). Blocks
  picked one by one (`toggleSelected(row)`: Ctrl+click, "One by one") are a list of uids instead,
  none inside another's subtree (a picked parent takes in its picked children; a child of a picked
  block cannot leave alone: "This block is selected with the block it belongs to"), both
  ends on the one picked last; a range picked into becomes its top blocks first, the block being
  edited joins (as Shift+click starts from it), one left is a range of one again, and a range
  (Shift+click, Shift+↑/↓, a drag, Ctrl+A, ↑/↓) replaces them, from the one picked last.
  `selectionPicked`: two or more picked; `selectionContiguous`: no gaps (picked blocks next to each
  other are a run like a range). The ends and the picked blocks
  follow edits, undo / redo (each step restores the selection it had: an undone deletion selects
  its blocks again — and stops the editing of another block meanwhile: the keys go to the
  selection) and changes from other devices; a zoom ends it; editing ends it; the page's
  properties are never part of a range nor picked. `selectionCount` counts the visible rows
  (folded children come along unseen). A command on the selection (Delete, Cut, a paste over it) goes from it to
  the next one without an empty selection between (the touch selection mode stays).
* **Ways in**: Esc in an edited block; Shift+↑ / Shift+↓ (from an edited block once its text
  selection reaches its start / end); Ctrl+A twice (Notion); Shift+click on any block — its text,
  a code or calculation frame, a table, a query, an embed, a rule, a bullet or number
  (`BlockEditor.blockTapped`, the click's modifiers) —, and Ctrl+click on any of them to pick that
  block into the selection or out of it (`BlockEditor.toggleBlock`, desktops); a drag
  with a mouse, touchpad or stylus from one block into another (`BlockEditor`'s tracker — a
  passive `PointHandler` over the list, the state in the editor because delegates may be destroyed
  meanwhile — scrolls near the view's edges; a left drag no longer scrolls the list on non-touch
  UIs, the wheel and the touchpad do — also in the journal stream); the block menu's "Select".
  Touch UIs: a **long press on a block** — its text, a code or calculation frame, a table cell, a
  query, beside an embed (not the block being edited: there the system's word selection and
  handles stay) — enters the **selection mode** — the keyboard goes, the selection's
  tools dock where the editing toolbar is; moving the finger without lifting it extends the
  selection (the list holds still, scrolls at its edges); taps on blocks move the selection's end
  there instead of editing; two round **grips** (44 px) at the band's start and end move its
  ends (a selection without gaps); the tools' **"One by one"** (a toggle, `BlockEditor.pickMode`)
  makes taps — and long presses — pick single blocks into the selection or out of it instead (no
  grips meanwhile; off again, a tap moves the end from the block picked last); "Done" and Back
  leave it (and "One by one" with it). The editing toolbar's "Select" and the bullet menu's "Select"
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
  "%n selected" are the stream's. It is paths and rows, not controllers: a day scrolled far away
  is destroyed and gets its part again when it comes back (`attach()` once loaded); meanwhile a
  copy reads it from its file. While dragging, the stream scrolls past the pressed day only as far
  as that day stays in the list's cache (half of it: `journals.selectionReach`) — longer spans:
  the wheel and Shift+click. **Copy only**: Copy / Ctrl+C / Ctrl+Insert and More → Copy
  as plain text take every day's blocks (`StreamSelection.copy()`), each day's after its link on a
  line of its own and a blank line (`[[2026-10-27]]`; plain text: the day's long date,
  "Tuesday, October 27, 2026"), a blank line between days, one HTML flavour rendered on
  request, the same 800 KB rule; the toast says "Copied %n block(s) from several days". Cut, Delete,
  One by one, the indents, moves and task statuses are shown unavailable (`BIconButton` /
  `BMenuItem.unavailable`, the reason as tooltip / description) — so are Cut, Delete, the
  indents and moves of the block menu of a block in the span, whose Copy / Copy as plain text
  copy the whole span (its count in the label) —, and they, Delete, Ctrl+X, Ctrl+V, Tab, Alt+↑/↓ and
  Ctrl+Enter say "Only copying works across several days"; Select all / Ctrl+A select
  the day's own blocks. The tools show on one day only — the focus day's (`toolsController`; the
  anchor day's while the focus day is away), which becomes the active day; the grips are hidden.
  Using one day on its own ends the span and clears the other days: a click or tap that edits,
  Ctrl+click, a selection made in that day alone, folding (any selection change the stream did not
  make), and a day leaving the list with an end of the span; Esc, Done, × and Back clear every
  day. Shift+↑/↓ with the focus day scrolled away scrolls there first (`revealRequested`). With
  every day of the span scrolled away (no tools shown: `toolsController` null) a tap edits as
  usual, which ends the span — no tap grows a span the user cannot see.
* **The band**: one band over the range from the editor's left edge (a block alone keeps its
  outline: from its own level, framed), rounded at its ends, a line down its left edge — blocks
  picked with gaps between them: a band for each run; made only for selected blocks (a `Loader`:
  "Scrolling performance").
* **Tools**: `SelectionToolbar` — "%n selected", Copy, Cut, Delete, Select all,
  One by one (docked only), More (Copy as plain text, QR code…, "Copy link" for one block, Indent /
  Outdent, Move up / Move down, Task status; the indents only where
  they can happen — `canIndentSelection()`: a document's paragraphs and headings have none; with
  gaps the indents and moves are shown disabled — `BMenuItem.unavailable`, the reason in a tooltip
  — and a tap says why in a toast: "Only blocks selected without gaps can be indented
  or moved"), Done. Docked in the editing toolbar's slot on touch
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
  only while both flavours fit 800 KB. On Linux also the primary selection. "Copy as plain text": plain text ("• ",
  numbers, ☐ / ☑ / ☒, emphasis marks in pairs gone, code spans verbatim, `((id))` as its block's
  text). A copy is read back (never "Copied" for a clipboard that did not take it — except in
  the browser, docs/WEB.md "Clipboard") and refuses more than 800 KB (Android's clipboard takes
  about 1 MB per transaction) with a toast — never cut short. Cut = copy, then delete as one undo
  step. The toasts "Cut %n block(s)" and "Deleted %n block(s)" (two or more blocks) have
  "Undo": the editor's `message(text, kind, action)` carries an `EditorToastAction` (`text`,
  `run()`; `BlockEditor.undoAction()`), the page views pass it to `AppController.toastAction` (which
  keeps the function; Main.qml shows the toast by id — `toastWithActionRequested`, the closure-free
  `toastWithAction` / `toastActionTriggered` and `toastRestore` of compiled QML go the same way)
  and the shell's toast shows the button; it runs `PageController.undoStep(token)`, which undoes that edit
  only while it is still the page's last step (`undoToken()` taken right after it names the
  history, the position and the step there) — after other edits (one made in its place after
  Ctrl+Z too), on another page or once undone it says "Not undone: the page has changed
  since".
  A toast's button takes no focus when pressed: the editor keeps the keys.
* **Acting on it**: Tab / Shift+Tab, Alt+↑/↓, Ctrl+Enter, Ctrl+Shift+Enter (the status menu) and
  the toolbar's Task status act on the range from one end to the other — on the picked blocks
  of a selection with gaps (Ctrl+Enter and the statuses; Tab and Alt+↑/↓ only without gaps, else a
  toast says why); a block selected alone keeps the block's own keys — Ctrl+Enter changes it, not
  the children it brings along (they are copied, cut, moved and deleted with it). Delete, Cut
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
  right-click opens Cut, Copy, Paste, Highlight…, Select all and Block… (desktops; touch UIs
  have the system's handles and popup, and the toolbar's "Highlight" — "Coloured highlights" below).
* **Part of several blocks** (the end of one paragraph and the start of the next): select the
  whole blocks and copy them, or select exactly those characters in the Markdown source (Ctrl+E,
  More → Markdown source) and copy them there.
* **Browser**: docs/WEB.md "Clipboard".
* **Tests**: core `tst_markdown_blockclipboard`, `tst_markdown_blockdoc` (ranges), `tst_io_html`;
  controller `tst_editor_selection` (the real clipboard read back; the HTML rendered only when
  asked for, once; blocks picked one by one, their copy, cut, delete, paste, undo, statuses, no
  moves with gaps); `tst_editor_streamselection` (the parts of a selection across days, keys,
  taps and drags over the days' edges, days destroyed and created again, the copy with the days'
  links); the journal stream's `journals-span-selftest` with real input (`tst_content_journals_span_qml`
  on a desktop, `_touch` on a phone); the playground's `selectiontest`
  with real keys, mouse and touch (`tst_editor_qml_selection` — Ctrl+clicks —, `_touch` —
  "One by one"); the journal stream's self-test; `scripts/android-emulator-test.sh --selection`.

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
  `markSaved`). A plugin file's pictures kept in their own files ("## Embedded Files") are shown:
  `DrawingCanvas.embeddedFiles` lists them, DrawingView resolves each link as the file's own links
  (`Workspace.resolveLink`) and hands the bytes to `provideEmbeddedFile` (kept across merges, never
  written into the file); the previews get them through the URL (below). New drawings are SVGs. A plugin file opened as a page (from the page list, a link,
  search) is shown as its drawing: `PageView.drawingPage` → the picture view, its "Edit drawing"
  opens this editor, "Show as text" the plugin's Markdown read-only — never the block editor.

* **The paper** (`DrawingCanvas`, a `QQuickPaintedItem`): the file's paper
  (`appState.viewBackgroundColor`, white), plain, lined (32 px) or squared (24 px), endless in every
  direction; the picture written covers the elements plus a margin. Opened: the elements from their
  top left at 100 %, zoomed out to the view's width when they are wider (a phone); a zoom other
  than 100 % is shown in the corner (`#535a6a` on light paper, `#9ba1af` on dark: 6.9 / 7.2:1).
* **Light or dark paper** ("Drawing background", per drawing: `DrawingCanvas.theme`, saved in the file as
  `appState.brulionTheme`, an undo step that marks it modified): **Like the app** (`auto`, the
  default: follows the app's theme — `appDark: Theme.dark` —, at once, also while a stroke is being
  drawn; the app's theme is no edit), **Light**, **Dark**. The stored colours never change: the
  dark paper (`#121212`) shows the strokes in Excalidraw's dark-mode colours (`drawing/palette.h`:
  the ink `#1f2430` as `#caced8`, every pen colour ≥ 4.2:1 on the paper; the lines and the grid
  `#323a45`), highlighter strokes in their own colour at 1.25 × the opacity (≥ 3.2:1 on the paper,
  the dark ink on them ≥ 2.9:1), the eraser's ring light (`DrawingCanvas.paperIsDark`, `paperColor`). The
  tool bar's swatches show the colours as the paper shows them (`displayColor`, as text
  `displayColorName`); what is chosen
  and remembered is the colour as drawn. The file's picture follows the choice (docs/SYNTAX.md §2:
  dark attributes for Dark; for Like the app an `@media (prefers-color-scheme: dark)` style sheet,
  so browsers show it dark when their viewer prefers dark, Obsidian in its dark theme). A merge
  with another device's version keeps the theme chosen here only when it was changed here since
  the last load or save (as the paper's pattern). No keyboard shortcut (Excalidraw's Alt+Shift+D
  switches its app's theme; this one is per drawing).
* **The paper's colour** ("Drawing background" → **Background colour…**; on phones in More; `DrawingPaperDialog`,
  `DrawingCanvas.paper`, saved as `appState.viewBackgroundColor`, an undo step; on Excalidraw's and
  Obsidian's drawings too): a grid of papers (neutrals, two rows of pastels, a dark row; white goes
  back to the default), a field for any `#rrggbb` and a preview with the default ink. The colour is
  stored as drawn, like everything else: on dark paper the grid and the preview show it mapped, a
  note says so and points to Light for the exact colour; a second note appears when the default
  ink has less than 3:1 on it (ink colours never change by themselves). The lines and the grid
  follow the paper (`displayRule`), the eraser's ring and the zoom's label its shade
  (`DrawingCanvas.paperIsDark`). A merge keeps the colour chosen here only when it was changed here
  since the last load or save (as the pattern and the theme).
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
  * **fingers:** with "Draw with finger" one finger works the tool (draws, erases, draws the lasso or a
    shape, moves the selection, taps text), else it only moves the paper — a palm resting on a
    selection never drags it; two fingers move and zoom — a second finger that lands while a
    stroke has just begun makes it the gesture (the stroke is dropped; a lasso or a shape being
    dragged too). A long press does nothing special. "Draw with finger" is on by default until a stylus
    has touched the paper on this device (AppSettings `drawing/stylusSeen`): from then on a palm
    or a finger never draws; the toggle (the tool bar on wide touch screens, More on phones)
    makes it the user's choice (`drawing/finger`: `auto` | `on` | `off`);
  * **palm rejection:** while the pen is down every touch is ignored — it neither draws, moves
    the paper, nor ends, cuts or erases the pen's stroke —, and so is a hand that stays after the
    pen lifts, until every finger has lifted. A pen that lands while fingers move the paper or
    draw ends their gesture (a finger stroke just begun is dropped) and draws.
* **Tools** (`DrawingToolbar`; above the paper on wide windows, at the bottom on phones — in
  the bottom navigation's place, `toolbarDocked` and `replacesBottomNav` —; when one row cannot
  hold them all — a phone held upright, `twoRows` — the tools and the swatch take a line of their
  own, centred, above Undo, Redo, More and Done, so nothing hides past the bar's edge; a row
  that still does not fit scrolls sideways): Undo,
  Redo, **Pen** (P), **Highlighter** (H: wide, translucent, an even width — constant pressure,
  as Excalidraw draws it alike), **Eraser** (E: removes every element it touches — a shape with its
  label, never a locked one nor a locked shape's label —, one undo step per gesture, a ring
  follows the pointer),
  **Select** (V, a lasso or a dashed square icon; clicked again while it is the tool it opens
  **Lasso** / **Rectangle selection**, `DrawingCanvas.selectMode`, remembered in `drawing/selectMode`;
  below), **Shapes** (one button showing the last shape — R Rectangle, O Ellipse, D Diamond, L Line,
  A Arrow and, after a separator and without keys, the polygons Triangle, Right triangle,
  Parallelogram, Trapezoid, Pentagon, Hexagon, Star, Block arrow, Speech bubble, Heart, Cloud,
  remembered in `drawing/shape` —; clicked again while a shape is the tool it opens their menu;
  both buttons carry a small caret, a `chevron-down` in the bottom-right corner of their square in
  `fgMuted` — `MenuCaret` —, the one sign that a menu lies behind them, phones having no tooltip),
  **Text** (T; below), the current tool's colours
  (pen, shapes and text: ink, blue, red, green, orange, purple;
  highlighter: yellow, green, pink, blue, orange) and widths (fine / medium /
  thick; narrow / medium / wide; text: its sizes Small / Medium / Large / Extra large, 16 / 20 /
  28 / 36 — an "A" at four sizes, `drawing/fontSize`) — on phones behind one swatch, a popup above
  the bar —, Paper (plain — a blank sheet's icon, `file`: the square is the rectangle's —, lines,
  grid: an undo step; and the backdrop's entries, below),
  **Drawing background** (a sun-and-moon, sun or moon icon: Like the app / Light / Dark; on phones in
  More: "Background like the app", "Light background", "Dark background"), Draw with finger (touch screens), Show the whole
  drawing (Ctrl+0; Ctrl++ / Ctrl+- zoom) and **Done**
  (saved, back). Colours and widths are remembered on the device (`drawing/…`). Undo: Ctrl+Z,
  Ctrl+Shift+Z / Ctrl+Y; every gesture and action is one step (a generic diff of the elements,
  `drawing::ElementDiff`, at most 500 steps); the history is the session's (a reload clears it).
  Undo and redo stamp what they put back as a new edit (a higher `version`, a new `versionNonce`
  and `updated`), so another device's merge and the Excalidraw plugin never take it for older.
* **Shapes** (rectangle, ellipse, diamond, line, arrow, and the polygons: `drawing::PolygonKind`):
  dragged out, shown while dragged; the colour is the pen's, the stroke twice the pen's width
  (1 / 2 / 4, Excalidraw's thin, bold, extra bold); Shift: a square, a circle, a polygon in a square
  box, a line at 15° steps; a click (under 3 px) makes none. Rectangles get Excalidraw's rounded
  corners, other shapes sharp; no fill (Style adds it). The tool stays (several shapes in a row) and
  the new shape is not selected. New arrows are never bound to shapes. A polygon always stands
  upright in the box dragged, whichever way it was dragged, and is Excalidraw's closed line
  (docs/SYNTAX.md §2 "Elements"): it has no kind in the file. It holds a label as a shape does (the
  text tool's tap inside it, a double tap with the select tool), stored as a text grouped with it so
  excalidraw.com shows it too (docs/SYNTAX.md §2 "Labels in polygons"): wrapped to its widest band
  across the middle (`drawing::labelMaxWidth`, `labelCenter`: a triangle's lower half, a callout's
  body), the polygon stretched from its top left when the label needs the room.
* **Selection** (Select, V): a lasso — or, with **Rectangle selection**, a rectangle dragged out — (dashed,
  in `Theme.info`, a 10 % fill) takes every element at
  least half inside it (its points; a box's corners, edge middles and centre), a tap the topmost
  element under it (8 px, 14 px for a finger or a pen; an unfilled shape only near its outline) —
  with its group (`groupIds`, the outermost) and a shape's label; a tap on nothing clears it.
  Locked elements — and the labels of locked shapes — are never taken. The selection is framed (dashed around all, thin around each
  when there are several); a drag inside the frame moves it (one undo step; arrows bound to it
  follow, labels go along). **DrawingSelectionBar** floats centred above the frame (below it when
  there is no room, always inside the view), touch-sized on touch screens, while nothing is being
  drawn, moved or written: the pen's colours (recolour; on narrow windows one swatch opening them
  — a highlighter stays one), **Style** (below), **Duplicate** (Ctrl+D, `copy-plus` — the plain
  `copy` is More's Copy, right under it on phones —: copies 10 units right and down, selected), **Bring to front** (Ctrl+Shift+]), **Send to back** (Ctrl+Shift+[: above a locked
  backdrop; both in More on narrow windows), **More** (**Copy**, Ctrl+C, **Cut**, Ctrl+X —
  as Excalidraw's clipboard, below —, **Rotate by 90°**, **Flip horizontally**,
  Shift+H, **Flip vertically**, Shift+V, **Group**, Ctrl+G, **Ungroup**, Ctrl+Shift+G: one outermost
  group added or taken away), **Delete** (Delete / Backspace). Keys: Ctrl+A (everything), Escape (a lasso or a shape in progress dropped,
  else the selection cleared), the arrows (move it 1 unit, with Shift 10), Ctrl+] / Ctrl+[ (one
  step up / down), Ctrl+Shift+← / → / ↑ / ↓ (align left, right, top, bottom), Alt+H / Alt+V
  (distribute across, down — Excalidraw's keys). Another tool clears the selection; undo, redo and a merge keep the selected
  elements that still exist.
* **The frame's handles** (`DrawingCanvas::selectionFrame`, core `drawing::resizedFrame`,
  `resizeElements`, `rotateElements`): the frame of one element is its own box (a text's, a
  picture's; a line's points), turned with its `angle`; of several, what they cover, unturned. On it
  squares at the corners and — on sides of 40 px or more, not for a text or several elements — at
  the middle of each side; a dot 28 px above it on a thin line, under it when the view has no room
  above (no dot for an elbow arrow). A press within 9 px (a finger or a pen 18 px) takes one, before
  anything else, also with Shift; on an element under 24 px across a press inside its frame moves
  it instead. A resize keeps the opposite corner or side where it is, never passes it and leaves at
  least 4 px; Shift keeps the shape — always for a text and for several elements, for a picture
  Shift lets it go instead. Rotating: about the frame's middle; Shift: the element's angle in 15°
  steps, several elements by 15° steps. While dragged the selection is drawn with its paths scaled
  or turned on top of the cached image (its lines too, and a label not yet wrapped again: nothing
  is computed until the end, so a long handwritten note follows at once); the release makes the
  edit — one undo step — and Escape drops it. The numbers: points and boxes scale along the
  element's own axes, line widths stay; a text scales its size and box only in a scale the same both
  ways; a shape's label is wrapped again in the new size (in a scale the same both ways scaled too)
  and the shape grows to hold it, as when it is written; an arrow's label stays level at its middle;
  the bound ends of arrows that are not selected get the same transformation. An edit that would
  leave `CoordinateLimit` is not made. Elbow arrows are moved, never turned. The selection bar stays
  clear of the handles (`selectionOuterRect`). A mouse over the frame gets the matching pointer
  (`updateSelectCursor`).
* **Style** (`DrawingStylePanel`, a popup from the selection bar, `DrawingCanvas.selectionStyle` and
  `styleSelection`, core `drawing::styleElements`): only the rows the selection has — **Fill**
  (none or six light colours, Excalidraw's; shapes and polygons) and **Fill pattern** (solid,
  hatched, cross-hatched; with a fill), **Line thickness** (shapes 1 / 2 / 4, the pen's 0.5 / 1 / 2, the
  highlighter's 3 / 5 / 8), **Line style**, **Corners** (rectangles Excalidraw's adaptive roundness,
  diamonds, lines and arrows its proportional one; a line or an arrow then curves, a polygon too;
  not elbow arrows), **Line start** / **Line end** (lines and arrows), **Text size**
  (the four sizes; laid out again, so a label's shape may grow), **Align** (two parts or more —
  an outermost group, a shape or an arrow with its label, any other element, each moved as a whole
  by the box it covers, a rotated element by its turned outline: `DrawingCanvas.alignSelection`, core
  `drawing::alignElements` —: icons for left, the middle across, right, top, the middle down, bottom;
  with three parts or more also **Distribute horizontally** / **vertically**, `distributeSelection`,
  `drawing::distributeElements`: the outermost parts stay, the others get equal gaps between their
  boxes, sorted by their middles — equal steps between their middles when they overlap too much, as
  Excalidraw distributes; bound arrows and labels follow as when moved), **Opacity** (100 / 75 / 50 /
  25 %; everything). Each row shows the first element that has it. One undo step a choice, the panel stays
  open, inside the view and scrolling when it does not fit; a choice that changes nothing is no edit.
* **Text** (Text, T): a tap on the paper writes a new text there (its first line's middle at the
  tap), a tap on a text edits it, a tap inside a shape or a polygon writes its label; with the select tool a
  double tap does the same. **DrawingTextEditor** opens on the paper in the text's font, size
  (× zoom), colour (as the paper shows it) and alignment — as wide as the text, a label as wide as
  it wraps in its shape (Excalidraw's getBoundTextMaxWidth: a rectangle's width, the widest
  rectangle inside an ellipse — width / 2 · √2 — or a diamond — width / 2 —, less 2 × 5), centred
  —; Enter starts a new line; Escape, Ctrl+Enter, a press on the paper beside it or (touch
  screens) its "Done" button write it — one undo step; an emptied text is deleted; a label grows
  its shape when it needs the room (as Excalidraw grows it, from its top left). A text closed as it
  was is no edit: nothing is stamped, wrapped again in Brulion's fonts, saved or added to the undo
  history. A locked shape's label is not edited (the text tool writes a new text there). On a phone the paper
  moves so the text stays above the keyboard (`DrawingCanvas.ensureVisible`, `KeyboardInsets`).
  New text: Excalidraw's Helvetica (sans in both apps) in the app's UI font, line height 1.15.
  While text is written the canvas takes no other input and the view's keys rest.
* **Backdrop** (notes on slides and PDF pages; docs/SYNTAX.md §2 "Backdrop", core
  `drawing/backdrop.h`): a picture, an exported slide or a PDF page under the ink. **In a note:**
  the slash command **"/Drawing on a picture or PDF…"** (`drawingOn`: also found as "slajd", "slajdy",
  "pdf", "podkład", "wykład", "tło", "slide", "annotate", "backdrop", "lecture") and the block menu's
  **"Insert a drawing on a picture or PDF…"** open the file picker ("Pictures and PDF"; `FilePicker` — the browser: its own
  picker, pictures only, `DrawingBackdrops.pickInBrowser`); the file is taken into the notebook
  (`EditorHost::importAttachment`, `Workspace::importAttachment`: a file of the notebook stays
  where it is — on Android also when the system's picker names it by a `content://` URI of the
  phone's storage, a memory card or Downloads' path, `platform/documentpaths.h` —, the same file
  already in the attachments folder — also under a " 2" name — is used again, anything else is
  copied there as pictures are). When no drawing comes of the pick (the page picker cancelled, a
  locked or unreadable PDF, a picture this device cannot decode) the copy made for it is deleted
  again (`PageController.discardBackdropSource` / `Workspace.discardImport`: only a copy that pick
  stored, never a file of the notebook or one reused). A picture becomes one drawing ("slajd
  (drawing).svg", light — **Light**) and opens in the editor; a PDF opens
  **DrawingBackdropDialog** (a centred card, a bottom sheet on phones): the file's name, "N pages",
  the pages as thumbnails (the image provider `pdfpage`, `PdfPageImageProvider`:
  `image://pdfpage/<file>?page=N&v=…`, drawn as they scroll into view; every cell the first page's
  shape — `DrawingBackdrops.pdfInfo` `width`/`height` —, wider cells for landscape slides), a tap
  chooses or drops a page — with the keyboard Tab reaches the pages, the arrows move (a focus
  ring), Space or Enter chooses; in the one-page dialog the arrows choose at once —,
  **All** / **None**, the field **Pages** ("1-3, 7", "all" — also "wszystkie"; in step with the
  thumbnails), at most 60 pages ("At most 60 pages at once."), **"Insert drawings (N)"**: one drawing
  per page ("wykład 3 p. 4.svg", as the app — **Like the app**), the first at the caret, every
  further one in a block of its own after it, in order — one undo step
  (`PageController.insertDrawingsOn`; the files stay, as with "/Drawing"); one drawing opens in the
  editor, several stay in the note with a toast ("Inserted N drawings"); when another page was
  opened in that editor while a picture was prepared, nothing is inserted (a toast; the imported
  copy goes). The pages are prepared
  in the background (`DrawingBackdrops.prepare`: Qt's thread pool, the browser one per event-loop
  turn) with a progress bar ("Page 3 of 12…") and **Cancel** — inserted drawings' files made
  there too (`prepare(…, drawings)`: core `newDrawingOnBackdrop` written — the raster's two base64
  copies and the scene's deflate), so the GUI thread only stores them and 60 slides freeze no
  window; a PDF with a password ("This PDF
  is protected with a password: Brulion cannot open it."), one that cannot be read and a picture this
  device cannot decode say so. **In the editor:** the paper's menu (on phones More) has
  **"Backdrop from a picture or PDF…"** (set or replace: a PDF's page in the same dialog, one page,
  **"Set backdrop"**), **"Another PDF page…"** (when the backdrop is a PDF's page and PDFs can be
  drawn here; the file renamed or gone: "The file … is not in the notebook any more." — renames do not follow
  into drawings) and **"Remove backdrop"** (red and last in its menu, after a separator) — each
  one undo step (`DrawingCanvas.setBackdrop`,
  `removeBackdrop`; `hasBackdrop`, `backdropPath`, `backdropPage`), and on a PDF's page **"Previous
  PDF page"** / **"Next PDF page"** (PgUp / PgDown; `DrawingView.stepPage`): the drawing of
  page N − 1 / N + 1 of the same PDF in the note this drawing belongs to — the note it was opened
  from (the route's `from`: PageController::openDrawing sets it) when that embeds it, else the first
  note linking to it (`Workspace.noteOfDrawing`) —: the first drawing that note embeds on that page
  opens (`Workspace.drawingsOnPdfPage`); none — a new one is made as "Draw on this page" makes it
  ("<PDF> p. N.svg", Like the app, the page prepared with the progress line) and embedded in that note
  in a block of its own right **before** (the previous page) or **after** (the next) the block that
  embeds this drawing (`Workspace.addDrawingBeside`: `notebook::insertBlockBeforeEmbed` /
  `insertBlockAfterEmbed`, compare and swap; no note: today's journal, with a toast). This drawing is
  saved first; the other one replaces it in the route (Back returns to the note). Page 1 has no
  previous page, the last page no next one ("This is the first / last page of the PDF."); a PDF that is
  gone or unreadable says so. Where PDFs cannot be drawn (the browser) the entries are not there, as
  "Another PDF page…". A thin progress line runs
  over the paper while a picture is prepared; one ready during a stroke, a shape or a text is set
  when that ends, one for a drawing closed meanwhile is dropped (its imported copy too), and one
  that would make the file unreadable is refused with a toast ("tooLarge"). The backdrop is locked: the
  eraser, the lasso, a tap and Ctrl+A never take it, "Send to back" stops above it. In the dark it is
  shown **"document dark"** (Excalidraw's dark filter per pixel, `darkImage`, decoded once per
  backdrop: a white slide `#121212`), so the ink mapped light stays readable; a Dark drawing's
  file gets that dark raster in its picture (encoded once, `drawing::SvgOptions`), a Like the app one
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
  file). Sizes: docs/PERFORMANCE.md "Size".
* **Clipboard** (`DrawingCanvas.copySelection`, `cutSelection`, `paste`; core `drawing/clipboard.h`,
  docs/SYNTAX.md §2 "Clipboard"): Ctrl+C / Ctrl+X and the selection bar's More put the selection on
  the system clipboard as Excalidraw's clipboard (`{"type":"excalidraw/clipboard",…}` as text), so
  excalidraw.com — and another drawing in Brulion — pastes it; Ctrl+V and **Paste** (the paper's
  menu; on phones More) paste Excalidraw's elements (copied in excalidraw.com, a `.excalidraw`
  file's text, Brulion's own) with new ids, their middle at the mouse pointer over the paper, else
  in the middle of the view — selected, the select tool chosen, one undo step —, or plain text as a
  new text there. Elements Brulion does not draw (frames, embedded pages) are left out with a toast
  ("N pasted elements Brulion does not draw (frames, embedded pages) left out."); a clipboard with nothing for a drawing says so.
* **Saving:** by itself, 1.2 s after the last change, and when the route changes, the view goes
  (Back, Done, another view) and the app leaves the foreground (`Workspace.writeAttachment`:
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
  "Merge drawings", which has no common version: a union, see "Sync visibility" → "Conflict copies
  of drawings"; Nextcloud's sync merges against the version both devices had last)
  and saved, with a toast. A gesture in progress goes on and ends on the new version. A version
  Brulion cannot edit any more (a frame or an embedded page added in excalidraw.com) is
  shown as it is; what was drawn here and not saved is written to a new file beside it ("… (this
  device).svg" — or `.excalidraw`, `.excalidraw.md`: the file's own format —,
  `Workspace.createAttachment`), with a toast naming it. A plugin file's Markdown is theirs then
  (their back of the card, their text entries), the merged scene in it. The canvas's own saves
  coming back are recognised (`isKnownFile`). A rename follows the file (the route is applied one
  event-loop turn late, as in PageView); a file deleted while open is never written again ("The
  drawing … is not in the notebook.").
* **Files Brulion does not edit** are shown as the picture they are, with why and "Open as a
  picture" (the picture view): an Excalidraw drawing with frames or embedded web pages (or an
  element of a kind Brulion does not know: it would lose them — "has frames or embedded web pages
  from Excalidraw"), an SVG without a scene, a scene that cannot be read, and elements
  farther than 10⁶ units from the origin (`drawing::CoordinateLimit`: the paper's lines and the
  file's size stay bounded; the editor's own elements stay within it). Nothing is written for
  them. A drawing of Obsidian's Excalidraw plugin with frames or embedded pages says to change it
  in Obsidian ("has frames or embedded pages, which Brulion does not edit yet"), one in the
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
  unless it is dark already (its edge darker than mid grey: a "Dark" drawing whose scene cannot
  be read keeps its dark picture). A `.excalidraw` scene or a plugin file has no picture of its
  own: one Brulion draws is rendered so in the frame of its `pictureRect` (the size
  `editor::localDrawingSize` gives the embeds and the picture view, on the GUI thread, once per
  version of the file: a 300 KB plugin file of 1500 strokes ~45 ms on the laptop, mostly Qt's JSON
  parser — `tst_editor_picturesize largeDrawingSizeIsCached`); one it does not draw (frames)
  shows the plugin's export beside it (`Szkic.excalidraw.svg` / `.png`, `editor::drawingExportOf`)
  or else its elements with a dashed grey box for each of the others (`drawing::previewDrawing`) —
  never a blank picture. A plugin drawing's pictures in their own files ("## Embedded Files") are
  named in its URL (`&f=<fileId>:<absolute path>`, the links resolved by `Workspace.drawingUrl`, the
  file's entries read once per version, the links on every call) and shown in place of grey
  placeholders. A `.md` file is read only when it is a plugin drawing. It runs in Qt
  Quick's image reader thread (the browser: the GUI thread) and only reads the file. A theme switch loads the other URL; `retainWhileLoading` keeps
  the shown picture — and the embed's frame its size — until it is there.
* View contract: `title`, `editing` (always), `toolbarDocked` (phones),
  `edgeSwipe` (no drawer edge strip: a stroke from the left edge stays a stroke).

Tests: `tst_drawing` (core: the format, Excalidraw's own fixtures, the outline, the SVG, the dark
colours — of another program's SVG too), `tst_drawing_exmd` (LZ-String against lz-string's own
vectors and the fixture generator's port, malformed and random input; the plugin's files —
compressed, json, CRLF, commented text, a frame, the oldest form — read, written back with every
byte around the block kept, deleted elements, text entries following edits — a text only moved,
recoloured or carried by its shape keeps its Markdown (links, transclusions), also across saves —,
crafted files read in linear time, the pictures of "## Embedded Files", the one API of drawing files, the `.excalidraw` layout — type
first, still a drawing beyond 4 KB), `tst_drawing_elements` (every kind of element read —
`excalidraw_shapes.*` —, malformed ones normalised or foreign, written back byte for byte, a
changed one's version, new elements as excalidraw.com reads them, fractional indices, corners,
arrowheads, hatching, hit tests, the lasso, every edit, undo diffs of random edits, the SVG of
shapes and text, random elements; aligning and distributing — groups, labels, rotated, locked,
overlapping, bound arrows, nothing stamped when in place —; labels in every polygon — inside, grown,
moved, turned, mirrored, resized, deleted — and how the scene stores them: grouped, read back, copied
in excalidraw.com, marks and groups that make no label), `tst_drawing_clipboard` (Excalidraw's
clipboard both ways: what is copied, pasted back with new ids, groups and bindings, excalidraw.com's
own with a frame, an embedded page and deleted elements, a scene, refused and malformed text, the
limit), `tst_drawing_merge` (the merge: new here on top, changed here
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
`alignAndDistribute`, `polygonLabelsWithTheTextTool`, `copyAndPaste` (the system clipboard),
`excalidrawMarkdownEmbeddedPictures`, `imageProviderEmbeddedPictures` (`tst_drawingcanvas`),
`drawingsOfNeighbouringPages` (`tst_workspace_pdf`), `insertBlockBeforeEmbed` (`tst_notebook_embeds`),
`excalidrawDrawingsAsPictures` (`tst_io_export`: HTML website and document, Pandoc),
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

## Database views (`qml/editor/DatabaseView.qml`, `src/app/editor/viewresultmodel.h`, core `views`)
## Canvas view (`qml/canvas`, `src/app/canvas`)

* **Header**: the kind's icon and name (a base: the view's name), the row count ("12 of 40 rows"),
  a spinner while the notebook is indexed, a base's other views (a menu; on its page also tabs),
  a calendar's month with its arrows (a tap on the month: today's), "+" (a new row: a field for its
  title under the view; Enter makes it, Escape or an empty field drops it) and Columns (add a
  column: the notebook's page properties most used first, or "Other…" — a name or `Label =
  expression`, a base's property id —; hide one; remove the sort). Under it the base's problems,
  the view's notice (a map shown as a table) or why nothing shows.
* **Table**: fixed rows (32 px, a finger's 44 on touch screens), the title column 220 px (180 on
  touch), numbers, dates and checkboxes narrower; wider than the block, it scrolls sideways (a
  horizontal Flickable inside the page's vertical scrolling, as wide tables do). The header: a tap
  sorts by the column (ascending, descending, none), the right button or a long press opens Sort
  ascending / descending, Move left / right and Hide column — all written into the query's
  `sort:` / `columns:` or the base view's `sort:` / `order:` (`views::setQueryOption`,
  `views::setBaseViewSort` / `setBaseViewOrder`): in a block through `PageController.setEditText`
  (one undo step), a .base file through the host (a toast's Undo). A grouped table has each group's
  heading line ("ŁAZIENKA · 3"), summaries a line under the columns ("Sum: 1599.5"). A tap on a
  link in a cell follows it, on the first column opens the row, on an editable cell edits it: a
  field over the cell (`BTextField`: Enter or leaving it writes, Escape drops — both give the keys
  back to the page's editor list, on a base's page to the view; a list as "a, [[B]]"; **Tab /
  Shift+Tab** write it and edit the next / previous cell that has a field — along the row, then the
  next row, the title column, checkboxes and a task's status and priority (menus) skipped —, **Up /
  Down** the cell above / below in the same column and **Left / Right**, when the caret is at the text's
  edge, the one beside it (`DatabaseView.editBeside()`; the table scrolls sideways and the page up or down to
  it; Tab with no cell left ends the editing as Enter does)), a checkbox
  toggles at once, a task's status or priority comes from a menu. While a cell
  is edited the rows wait for it (`ViewResultModel.hold`).
* **Board**: a column per group (260 px, 232 on touch; the board scrolls sideways), its heading
  with the count, the group's first summary and "+" (a new row with the column's value); cards with
  their cover, the first column (two lines) and up to three other columns (one line each, the
  group's own left out). A card is dragged with the mouse at once, on a touch screen after a long
  press (400 ms — the page keeps scrolling otherwise; `MouseArea.preventStealing` while it drags); a
  copy of it follows the pointer, the columns that take it are tinted, the board scrolls near its
  edges, and dropping it on another column writes the column's value (`views::moveEdit`). A tap
  opens the card's row.
* **Gallery**: cards of a fixed size (200 px; two per row on a phone) with the cover (a picture:
  `EditorHost::assetUrl`, a web picture once cached; a colour; else an empty picture's icon).
* **Calendar**: weekday names, a 6-week grid (`views::calendarRange`, weeks start on
  `weekStartsOn`), days outside the month on the sunken surface, today's number in the accent;
  each day lists up to three entries (two on touch) and "+N more". An entry is dragged to another
  day as a card is (`views::dateEdit`); a double tap on a day makes a new row on it.
* **Dataview calendar** (a ```` ```dataview ```` block whose query is `CALENDAR …`,
  `dataview::isCalendarQuery`: `QueryRole.view` "dataview", `ViewResultModel.language` "dataview"
  over `QueryHost::runView("dataview", …)` → `notebook::runView`; docs/SYNTAX.md §10 "Dataview
  queries"): the same grid and month arrows, read-only (no columns menu, no "+", nothing to drag);
  a day shows a 6-px accent dot per page (as many as fit, then "+N"; `viewDayDot`), a tap on a day
  with one page opens it, with several a menu of its pages (`viewDayMenu`; `dayMenuOpen`); a day
  is a button for screen readers ("20: 2 pages").
* **List**: one line per row, its first column and the others after "·".
* **Edits** (`ViewResultModel.editCell` / `moveRow` / `moveRowToDay` / `addRow`): a task's status
  through `PageController.setTaskStatusAt` (as a checkbox: the status history, the next instance of a
  recurring task); anything else with `views::applyRowEdit` — on the page holding the view as one
  undo step of the page (`PageController::applyContentEdit`), in another file through the host,
  with a toast "Changed “…”" whose Undo (`EditorToastAction` "viewUndo"; on a base's page
  `AppController.toastWithAction`) writes the file back while it holds what was written. A refused
  edit (a Polish key on a block's line, a number that is none, frontmatter that cannot be edited
  safely) says why in a toast.
A ```` ```query ```` block with `view:` (docs/SYNTAX.md §10 "Views"), a ```` ```base ```` block, a
`.base` file opened as a page (§10 "Obsidian Bases"; `ContentBaseView`, PageView's `baseRoute`) and
one embedded in a note (`![[Books.base#Reading]]`: BlockListModel's `base` embed, EmbedView —
`thisPath` the note, `this` in the base's expressions; edits and toasts as in a block) draw
a `DatabaseView`: `ViewResultModel` runs the view through the host (`QueryHost::runView` —
`Workspace` keeps a `QueryCache` per query view like query blocks, and a base's last result per
index version and minute; `notebook::runView` gives the JSON, the command line prints the same),
renders the cells' Markdown with the page's renderer and gives the rows, the columns and the
groups as models. In a block it takes the place of the query's result list under its header and
filter chips (QueryBlockView: the query's `QueryResultModel` does not run); its height comes from
bindings and the first run comes when the model is completed, so the block is created with its
final height ("Scrolling performance").
Route `canvas` (`{path}`, optional `line`): `CanvasView` on a whiteboard file `*.canvas` (docs/SYNTAX.md
§2 "Canvases": JSON Canvas, Obsidian Canvas; core module `canvas`) — text cards, notes, pictures,
drawings, PDFs, other files, web addresses and groups on an endless canvas, connected by edges. A
`page` route naming a canvas becomes a `canvas` route (`normalizeRoute`; `asFile: true` keeps it a
page — "Open as text" for a file that is no canvas), so the file tree, links, search, backlinks,
the quick switcher and tabs open canvases here. `line` (a search hit's or a backlink's line of the
canvas's index text) selects that card and zooms to it (`CanvasEditor.focusLine`). "New canvas"
(the command palette, the sidebar's pages menu, "New canvas here" in a folder's menu:
`Workspace.createCanvas`) makes `Untitled canvas.canvas` and opens it.

* **The board** (`CanvasEditor`, a `QQuickItem` without content of its own): the document (core
  `canvas::Canvas`), its undo history (a snapshot per step, 200 steps; a gesture is one step at its
  end), the view (`zoom` 5 %–400 %, `panX` / `panY`: screen = canvas × zoom + pan; moves to fit
  animated unless motion is reduced), the selection and every pointer gesture. It lies under the
  cards, which take no input themselves, and hit-tests the canvas: side handles, the corners and
  sides of the one selected card, cards (groups only by their frame — a 14 px band — or label:
  a group's inside is the canvas, so a rubber band can be drawn there), edges (within the handle
  radius). Opened: the whole canvas in view, at most 100 %.
* **Only what is in view is made** (docs/PERFORMANCE.md): `CanvasEditor.nodes` and `.edges`
  (`CanvasNodeModel`, `CanvasEdgeModel`) list the cards and edges in the view and half a screen
  around it, in z-order; after a change of the canvas, the view or the selection they change rows in
  place, insert those that come into view and remove those that leave, so a canvas of a thousand
  cards has the few dozen on screen as QML items. The cards and edges live in one item the board
  moves and scales (`world`): panning changes its position, nothing is laid out again. Zoomed out
  below 30 % (or a card smaller than 48 × 28 px on screen) a card shows only its colour and title,
  large (`CanvasEditor.detailed`); its content is neither rendered nor loaded.
* **Cards** (`CanvasCard`): a frame in the card's colour (presets as Obsidian shows them, lighter in
  the dark theme; a file's hex colour as it is: `CanvasColors.hex`). What they show comes from
  `CanvasCardContent` / `CanvasCards` (C++), kept per text and note until the index or the file
  changes: a **text card** its Markdown rendered as an embed is (`BlockListModel::textPreview()`:
  InlineRenderer's escaped rich text — never a note's own HTML —, headings, lists, tasks with
  their status, quotes, code, embeds named; links follow on a click); a **note** its page, or the
  section or block of its `subpath` (`BlockListModel::linkPreview()`), under its name; a longer
  text scrolls with the wheel while the card is selected (a thin bar shows where); a **picture** the
  picture (decoded at about the size shown, a few sizes, not each zoom), a **drawing** its preview
  in the app's theme (`Workspace.drawingUrl`), a **PDF** its first page's thumbnail
  (`EditorHost::pdfThumbnail`: the PDF thumbnails' cache) with its name and page count, another
  **canvas** its picture (`CanvasImageProvider`, image provider `canvas`), any other file its icon
  and name, a missing one what is missing; a **web address** its host and address — the page is
  never fetched (no safe opt-in way to show other sites exists in the app); a **group** its frame,
  tinted, its label above it (larger as the view zooms out) and its background picture (cover,
  ratio, repeat).
* **Edges** (`CanvasEdge`): Bézier curves out of the cards' sides (`canvas::edgeShape`: the named
  sides, else those facing each other), with arrowheads at the ends that have them, in their colour
  (else the theme's faint line), as Qt Quick Shapes in canvas units; a label in the middle on a
  patch of the canvas's colour; a selected edge has an accent ring.
* **Mouse:** a click selects a card or an edge (Shift / Ctrl adds or removes it); a drag moves the
  selection (a group with the cards inside it); a drag on empty canvas draws a rubber band (cards
  it touches; a group only when it holds the whole group; Shift adds); the middle button or Space
  held moves the canvas; the wheel and a touchpad move it, Ctrl + wheel and a touchpad's pinch zoom
  at the pointer, a touchpad's smart zoom fits; the four handles on the sides of the card under the
  pointer connect: dragged onto a card, an edge to its nearest side (selected, to label or colour),
  dragged onto empty canvas, a new text card there, connected, to type into; the corners and sides
  of the one selected card resize it (at least 40 × 40). A double click edits a text card's
  Markdown in place, opens a note or file (`subpath`: at that heading or block), opens a web address
  in the browser, renames a group, labels an edge, and on empty canvas adds a text card there. A
  right click: the context menu (add a card or a note here, edit, open, open in a new tab, change a
  web address, label, copy, cut, paste, duplicate, group / ungroup, to the front / back, select
  all, show the whole canvas, delete).
* **Touch** (phones, tablets): one finger moves the canvas — or the selection, when it starts on
  a selected card; a handle connects, a corner resizes —; two fingers move and zoom; a tap selects;
  a double tap does what a double click does; a long press opens the menu; "Select several" in
  the tool bar makes taps add to the selection and a drag draw a rubber band. Handles are larger.
* **Keys** (none while text is typed): Ctrl+Z, Ctrl+Shift+Z / Ctrl+Y, Delete / Backspace, Ctrl+A,
  Escape (a gesture dropped, else the selection cleared), Ctrl+C / Ctrl+X / Ctrl+V, Ctrl+D
  (duplicate), Ctrl+G / Ctrl+Shift+G (group, ungroup), Enter (edit the selected card), the arrows
  (move the selection by 10, with Shift by 1; with nothing selected the canvas), Ctrl+] / Ctrl+[ /
  Ctrl+Shift+] / Ctrl+Shift+[ (z-order), Shift+1 (the whole canvas), Shift+2 (the selection), Ctrl+0
  (100 %), Ctrl++ / Ctrl+- (zoom), Space held (the mouse moves the canvas).
* **Text in place** (`CanvasTextEditor`): over the card, in the canvas's scale; Escape, Ctrl+Enter or
  a click outside ends it and keeps the text (as Obsidian); the card grows to hold it. One undo
  step. Labels and addresses: `CanvasTextDialog`.
* **The tool bar** (`CanvasToolbar`; above the board, at the bottom on phones where it takes the
  bottom navigation's place): undo, redo; add a text card (in the middle of the view, typed into),
  a note (the page picker: a page, or a new one created), a file (the system's file picker, the file
  imported into the notebook — or used where it is in it), a web address, a group (around the
  selection, or an empty one); "Select several" (touch); zoom out, the zoom (a click: 100 %), zoom
  in, the whole canvas; More: paste, select all, zoom to the selection, export as PNG / SVG.
* **The selection's bar** (`CanvasSelectionBar`, floating above the selection, below it near the
  top): colour (none, the six presets), edit / rename / label, open, arrowheads (none, one way,
  both ways, reverse), duplicate, group / ungroup, align (left, centres, right, top, middles,
  bottom; space evenly across or down), arrange (front, forward, backward, back), delete.
* **Adding by drop and paste:** files dropped from another app are imported and added where they
  land, side by side; a page dragged from the sidebar's tree and let go on the board is added as a
  note card there (`AppController.dropOnView` → `fileDroppedOnView`). Ctrl+V: cards copied on a
  canvas (Brulion's or Obsidian's JSON Canvas fragment: new ids, at the pointer), files copied in a
  file manager (imported), a picture (a PNG in the attachments folder), a web address (a link card),
  any other text (a text card sized to it). Ctrl+C copies the selected cards (a group with its
  cards) and the edges between them as such a fragment.
* **Saving:** 0.8 s after the last change (not while text is typed or a gesture runs), when the route
  changes, the view goes and the app leaves the foreground (`Workspace.writeAttachment`) — the file
  in its own layout, only what changed written differently (docs/SYNTAX.md §2 "Canvases"). Every
  save first reads the file: another program's version (Obsidian, Syncthing, Nextcloud) not yet
  reported is merged in, never written over; a version reported while the canvas is open is taken in
  as it is when nothing changed here, else merged card by card and field by field
  (`CanvasEditor.takeExternal`: `canvas::merge` against the version last loaded or saved; the undo
  history starts anew). A file that is no canvas is shown with why ("Open as text") and never
  written; what was done here before it turned so is kept beside it ("… (this device).canvas").
  Renames follow the file; a deleted file is never written again.
* **Export:** More → "Export as PNG picture" / "Export as SVG picture" writes `<name>.png` /
  `<name>.svg` beside the canvas (`CanvasEditor.exportPicture`: core `canvas::toSvg` — cards,
  groups, edges and labels, the pictures of its cards embedded, the first lines of its notes —; the
  PNG at twice the size, at most 8192 px a side), in the app's theme.
* **Elsewhere:** the sync center shows two versions of a canvas in conflict as pictures and merges
  them ("Merge canvases": a union card by card, `ConflictResolver`); the HTML exports show a canvas
  as its picture (`Plan.canvas.svg` beside it: the cards with their text, the pictures and the
  notes' first lines they show, groups, edges and labels) where a note embeds it, and their links
  to it lead there.

## PDF viewer (`qml/content/ContentPdfView.qml`, `src/app/pdf`)

A PDF of the notebook (`paths::isPdfPath`: ".pdf" in any case) has a viewer of its own, like a
picture has the picture view: route `page` with the PDF's path (`PageView.pdfRoute`; the block
editor stays unloaded — `page.path` "" — so a PDF's bytes never reach it, where a typed character
would have been saved over them). Drawn by this build's PDF renderer (`src/platform/pdfpages.h`:
Qt PDF on Windows, the AppImage and CI, poppler in the Nix package, Android's own PdfRenderer; the
browser has none — see "In the browser" below).

**Embeds.** `![[wykład.pdf]]` (and `![](assets/wykład.pdf)`) in a block is the thumbnail of its page
— `![[wykład.pdf#page=3]]` (Obsidian's syntax) the third, `#height=…` accepted and ignored
(docs/SYNTAX.md §2) — `PdfEmbedTile.qml`: a white page with a `borderSubtle` frame (also in the dark
theme: a page is paper), sized from the page's shape (A4 portrait 297 × 420, a 16:9 slide 480 × 270,
at most 420 high and the column's width; known from the start once cached, so the block does not
jump), under it a caption row: the page icon, the file's name (plain text, elided in the middle),
"3 pages", "p. 3" for a page > 1. A spinner while it is drawn; a lock and "PDF protected with a password" or
a triangle and the reason on the white page for one that cannot be shown. A tap / click (the tile;
the space beside it still edits the block) emits `EmbedView.pdfClicked(path, page)` →
`PageController.openPdf(row, path, page)`: the page is saved, route `{view: "page", path, pdfPage
(> 1), from: the note, fromLine: the block's first line}`. A link `[[wykład.pdf]]` /
`[[wykład.pdf#page=3]]` opens it the same way (`activateLink`: the fragment's page, `from` /
`fromLine`, no `heading`). The thumbnails come from `EditorHost::pdfThumbnail(path, page)` (the
embed map's `pdf`, `page`, `viewable`, `state` "loading" | "ready" | "failed" | "unsupported", `url`
— the cached PNG —, `width` / `height`, `pages`, `pageWidth` / `pageHeight`, `error`, `locked`;
`EditorHost::pdfThumbnailChanged(path)` recomputes only the rows naming it). Never `assetUrl()`: in
the browser that was a data: URL of the whole file. The shell's `Workspace` draws them with
`pdfview::PdfThumbnails`: off the GUI thread (`PdfRenderPool`, a reader leased from
`PdfReaderPool`), once per (file, size, mtime, page, pixel size), cached as PNG files in
`<app cache>/pdf-thumbnails` (PNG text chunks `Brulion-Pages`, `Brulion-PageWidth`,
`Brulion-PageHeight` read back without decoding; a locked, unreadable or page-less PDF as
`<key>.err`, so it is not opened again on every scroll, while a failed drawing — out of memory — is
kept in memory only and tried again after a restart; a remembered thumbnail whose PNG was pruned is
drawn again; a new mtime is a new key; 64 MiB, 32 MiB on Android, the oldest
pruned 2 s after a store; the long side 420 px × the primary screen's ratio, rounded up to 512 / 768
/ 1024 / 1280 / 1536). A build without a renderer, the browser: the file's chip with the page icon
("Open in new tab" / "Open in another app" in its tooltip); its click opens the PDF in a
new browser tab / the system's viewer, inside the click. A thumbnail folder that cannot be written
(a renderer is there): the same chip, its tooltip "Open in the PDF viewer" and its click the
viewer.

**The viewer.** A bar like the picture view's: the page icon and the file's name, then — at 600 px
and wider — "Page 3 of 12" (a click: the page jump), Zoom out, the zoom "85 %" (its menu: Fit
width, 50–400 %, Two pages side by side, Cover page alone), Zoom in, Search (only where the
renderer extracts text), Contents (only when the PDF has one), **Draw on this page** and ⋯
(Open in another app); narrower (phones, the side panel): "3/12", Search, Contents, ⋯
(Zoom in, Zoom out, Fit width, Two pages side by side, Cover page alone, Draw on this
page, Open in another app). The
pages one under another on `bgSunken`, each white with a hairline frame, laid out exactly by
`PdfPageLayout` (every page its own size; fit width by the widest page, at most 1000 px on wide
windows — readable lines —, the full width on phones; zoom shown as the real size, 96 dpi = 100 %,
10–800 %). Only the pages within half a view above and below exist (`PdfVisiblePages`, rows added and
removed at its edges, so the Repeater keeps its pictures); their pictures come from the image
provider `pdfview` (`image://pdfview/<file>?page=N&v=<mtime-size>&w=…&h=…[&clip=…]`,
`PdfViewImageProvider`: asynchronous, cancelled when scrolled away, a page before a detail picture,
`cache: false`). Memory stays at about four screenfuls of pixels at any zoom
(docs/PERFORMANCE.md "PDF viewer"): a page's base picture has at most a screenful of pixels;
zoomed in, the visible part comes at full resolution as a detail picture once the view stops moving
(150 ms). Under the last page the notes that embed or link the PDF (`ContentBacklinks`; not while
the wide window's side panel lists them), "Showing the first 20 000 pages." beyond that many.

* **Zoom**: pinch (the pages scaled on the GPU while the fingers move, drawn again once at the end,
  around the fingers), double tap (fit width ↔ twice that, around the tap), Ctrl+wheel (the same
  preview, committed 150 ms after the last turn, around the pointer), Ctrl++ / Ctrl+− and the
  buttons (around the view's centre), Ctrl+0 fit width; fit width follows the window's width.
  Zoomed past the view's width the pages pan sideways (a mouse drag, Shift+wheel, ← →), and on
  phones the drawer's edge strip yields to that (`PageView.edgeSwipe`).
* **Keys**: Ctrl+F search, Ctrl+G the page jump, Ctrl+C copies the selected text, Ctrl+A selects
  the current page's text, PageDown / Space and PageUp / Shift+Space by 90 % of the view, Home / End
  the first / last page, ↑ ↓ 48 px, Esc clears the selection, else closes the search, else goes back
  (Back on Android and Alt+← through the shell).
* **Page jump** (`pdfJump`): a dialog — a bottom sheet on phones — with the PDF's page count as
  its subtitle ("12 pages") and the page number, filled with the page shown and selected once the
  field has the focus, so a number typed replaces it; a number only, and a page the PDF has not
  is said at the field ("Enter a page from 1 to 12.") while "Go" waits; Enter jumps.
* **Search** (`pdfSearchBar`, where `Workspace.pdfSearchable`: Qt PDF, poppler, Android 15+): typed
  250 ms later the pages are searched on `PdfRenderPool` at the lowest priority from the current
  page, wrapping (`PdfReader::find`, `src/platform/pdfsearch.h`: case and Polish letters ignored —
  "gesla" finds "gęślą" —, a line break of the PDF equal to a space, soft hyphens ignored; a scanned
  page — fewer than 8 letters of its own — in the text OCR read on it, its hits placed by OCR's word
  boxes: "Searching attachments"); a result of the whole-notebook search opens here at its page with
  its words typed (route `open: "search"`, `search`, `pdfPage`: the first hit at or after that page
  is the current one; another result of the same PDF searches again from its page); hits come
  in batches, the first one at or after the current page is shown at once, highlighted in the
  accent (the current one stronger, framed). "3 of 12" ("+" while searching or past 5000 hits),
  "Searching… page 4 of 12", "No results", "This PDF has no text to search (it may be a scan)." (where
  OCR could read it but is off: "… Turn on text recognition in Settings → Search to search it.",
  on the line under the row in every bar — the row would cut it);
  in the narrow bar (phones, the side panel) only the counters stay in the row: the two long states
  take the line under it, full width (`pdfSearchStatusLine`; `pdfSearchStatus` holds the text in
  every state), and ‹ › show only with hits, so the field keeps its room and × stays on screen.
  Enter / F3 next, Shift+Enter / Shift+F3 previous, Esc (Back on Android while typing) closes it and
  clears the highlights. A new text cancels the search between pages.
* **Selecting text** (where `PdfViewModel.selectionAvailable`: Qt PDF and poppler; not Android's
  PdfRenderer — its selection needs API 35 and is not wired yet —, not the browser): a mouse drag
  over a page selects its text from the press to the pointer (`selectText(page, from, to)` in
  fractions of the page; the Flickable may not steal the drag, so the mouse pans no longer — the
  scroll bars, Shift+wheel and ← → do; the cursor is an I-beam); a point beside the text takes the
  nearest character (poppler: `pdftext.h` `pdfCaretAt` on its per-character boxes; Qt PDF: the point
  moved onto its nearest line, then pdfium's `getSelection`). A selection stays on the page it began
  on. A finger: a long press selects the word under it (`selectWord`: the characters a few points
  around it, then `pdfWordAt` of the page's text — letters, digits, marks, an apostrophe or a hyphen
  inside a word), and moving the finger without lifting it selects on to it (the pages stop
  scrolling meanwhile; a `PointHandler` follows the finger); a tap elsewhere ends it. The selection
  is tinted one blue on the white page in both themes; a small **Copy** over its first line, Ctrl+C,
  and the right-click menu (Copy, Select the whole page, Contents) copy it (`copySelection`:
  `pdfCopyText` — CR LF as LF, soft hyphens and pdfium's U+0002 dropped, trailing white space
  trimmed) with the toast "Copied to the clipboard"; Ctrl+A selects the current page's text; Esc
  clears the selection first. Each request runs on `PdfRenderPool` (priority Selection) and a newer
  one makes the older moot, so a fast drag never queues work.
* **Contents** (the PDF's outline, read once it opens: `PdfReader::outline()` — poppler's own, and
  in the Qt PDF builds (Windows, the AppImage) core `pdfoutline` reading the file's bytes: Qt PDF's
  `QPdfBookmarkModel` follows a looping outline until the stack overflows, ours visits each entry
  once, within MaxPdfOutlineItems and MaxPdfOutlineDepth; an encrypted PDF shows none there.
  Android's PdfRenderer has none: the button is not there): `pdfOutline`, a dialog (a bottom sheet on phones) with the entries indented by level, each
  with its page number, the entry of the current page marked and scrolled to; a tap jumps to the
  page and the place the entry names (`positionForTarget(page, top)`: that height, a gap above it).
  Titles are plain text, one line (`pdfCleanTitle`: control characters as spaces, at most 300
  characters); entries without a page are shown disabled; at most 5000 entries, 32 levels (a hostile
  outline that loops ends there).
* **Links** of the pages near the view (`linkRects(page)`, loaded as the pages come near:
  `PdfReader::links()`, at most 500 a page, those of 64 pages kept): a link to a page of this PDF
  (also a named destination) jumps there; a web or mail address (`pdfLinkUrlAllowed`: http and https
  with a host, mailto with an address — never `javascript:`, `file:`, an app's scheme, a launch
  action or a script) opens through `Workspace.openUrl`, the same check as a note's links, its
  address in a tooltip on hover. The pointer is a hand over a link; a link is a press target above
  the text selection.
* **Two pages side by side** (the zoom menu, ⋯ on phones; `PdfViewModel.twoPages`,
  `PdfPageLayout::setSpread`): pages 1–2, 3–4, … in rows, at the top of their row, fit width by the
  widest row; **Cover page alone** (`coverAlone`) puts the first page alone (1, 2–3, 4–5 — as a book
  opens). The current page stays in view and stays the current one; "Page N of M" then names the
  row's first page as it scrolls. Both remembered on the device (`AppSettings` keys `pdf/twoPages`,
  `pdf/coverAlone`).
* **Annotations** a PDF has (highlights, notes, stamps, shapes) are drawn with the page where the
  renderer draws them: Qt PDF (`RenderFlag::Annotations`) and poppler (`tst_pdfpages`
  `annotationDrawn`: a square with its own appearance); Android's PdfRenderer draws what its display
  mode draws (not checked on a device yet: TODO.md). Making new ones is for another app (Open in
  another app).
* **Open in another app** (⋯ and the top bar's More): `Workspace.openInOtherApp` →
  `platform::openFileExternally` — the desktop's default app, Android's ACTION_VIEW (a content:// URI
  of Brulion's FileProvider with a read grant; a file outside its roots, a memory card, copied into
  the cache first), the browser's new tab (a blob: URL typed `application/pdf`; a blocked tab: the
  file downloaded, a notice says so). Only a PDF of the notebook named `.pdf` whose first 1024 bytes
  hold `%PDF-` (a renamed program is never handed to xdg-open / ShellExecute). Messages: "“x” is not
  a PDF file.", "No app on this device opens PDF files.", "Could not open
  “x” in another app."
* **Draw on this page**: a drawing whose backdrop is the current page ("Drawing editor" →
  Backdrop: `DrawingBackdrops.prepare(pdf, [page], true)`, the button says "Preparing page 3…"
  meanwhile), stored as "<name> p. 3.svg" (`EditorHost::drawingFileNameOn`, as "Drawing on a picture
  or PDF…" names them) and embedded right after the block that embeds the PDF in the note it was
  opened from (`route.from` / `fromLine`; `Workspace.addDrawingOnPdfPage` →
  `notebook::insertBlockAfterEmbed`: that block, else the nearest one embedding it, else the end;
  a list item at the same depth or a paragraph; compare-and-swap writes) — opened from the tree, the
  quick switcher, or the note gone: today's journal ("The drawing was added to today's journal.").
  Then the drawing editor opens; Back returns to the viewer on that page (it writes `pdfPage` into
  the route), Back again to the note with the drawing right under the PDF. When the note embeds a
  drawing of this page already (`Workspace.drawingsOnPdfPage`: the drawings' backdrop markers), a
  menu offers "Open the drawing “…”" (up to three) and "New drawing on this page".
* **A password**: where the renderer opens protected PDFs (`PdfViewModel.passwordAvailable`: Qt
  PDF and poppler — tried as the owner's and the user's password, in UTF-8 for AES-256 and in
  Latin-1 for older encryption), a protected PDF says "This PDF is protected with a password." with a lock, a
  password field (`pdfPasswordField`, focused on a desktop), **Open the PDF** (`pdfUnlock`; Enter too)
  and "Brulion does not remember the password: it is asked for each time the PDF is opened."; a wrong one
  says "Wrong password. Try again." under the field (`passwordRejected`). The password
  is never stored: it lives in the model and, while the PDF is open, in `PdfReaderPool`'s memory
  (`holdPassword` / `releasePassword`, counted per file) for the image provider's pages; the field is
  emptied at once; another file, the viewer's end or a wrong password forgets it (and closes the
  readers opened with it). It is never in the route, a setting or a log line. The embeds' thumbnails
  of a protected PDF stay a lock (their PNG cache never holds its pages), and "Draw on this page"
  is not offered for it (the drawing's backdrop would need the password too). Android's PdfRenderer
  opens none (API 35's `LoadParams` is not wired yet): there the old message stays.
* **States**: a spinner while it opens; a PDF that cannot be shown — the password above, or where the
  renderer opens no protected PDF "This PDF is protected with a password: Brulion cannot open it." (a lock),
  "This PDF cannot be read.", "This PDF has no pages.", "The file is no longer in the notebook." —
  under the file's name and size, with **Open in another app** (another app may know the
  password or repair it) and the notes that embed or link it below (as in every state without pages;
  not while the side panel lists them);
  a page that fails alone says "Page N cannot
  be shown." on its white page. The file changed on disk (sync, another program): opened again at the same
  page and zoom; renamed or moved: the shell's route follows it and the viewer reopens on the
  route's page.
* **Route**: `pdfPage` (the page shown; the viewer writes it half a second after it settles, in the
  main pane only, and jumps to it when the route's page changes from outside — never for its own
  writes), `from`, `fromLine`, `open` (screenshots and tests: `search` with `search` typed, `jump`,
  `draw`), `pdfScale` (a zoom in percent, on its own too: once per path and value). Ctrl+E does nothing on a PDF; the top bar's More has no
  Markdown source, note mode, properties or history there.
* **In the browser** (no renderer): the embed is the file's chip; a click opens the PDF in a **new
  browser tab** (`wasm/wasmopen.cpp`: the bytes from IndexedDB as a blob: URL typed
  `application/pdf`, `window.open` inside the click — browsers open tabs only on a user's gesture —,
  the opener cut, the last three blob URLs kept for the tab's reload and "Save"); a blocked tab
  downloads the file with a notice. A route to a PDF (the tree, the quick switcher, history) shows
  "Brulion here does not draw PDFs: the browser's own viewer opens it in a new tab (scrolling,
  zoom, search)." with **Open in new tab** (a real click). A desktop
  build without a renderer says "This version of Brulion has no PDF renderer: open it in another
  app." with that button.

**Other attachments.** Any other file that is no note, no picture and no PDF (DOCX, ZIP, MP3…)
opens read-only as text (`PageView.fileRoute` → `ContentPictureView` with `textOnly`: the bar names
the file and says "Read-only", at most its first 200 KB) — never in the block editor. A kind
on the allow-list (`platform/externalfiles.h`: DOCX, XLSX, PPTX, ODT, ODS, ODP, EPUB, DOC, XLS, PPT,
RTF, MP3, M4A, OGG, OPUS, WAV, FLAC, MP4, MOV, WEBM, MKV, TXT, CSV, TSV — by its extension AND its
first 64 KiB: a zip with Word's / Excel's / PowerPoint's parts, an OpenDocument or EPUB zip naming
its own type first, OLE2, `{\rtf`, the audio and video signatures, text without NUL bytes in
valid UTF-8) has **Open in another app** in that bar (`pictureOpenElsewhere`; "Open
elsewhere" when narrow) and in the top bar's More (`Workspace.canOpenInOtherApp`): the system's app
for it with the list's MIME type (Android's ACTION_VIEW; the browser's new tab, which usually
downloads it). A program, a script, a shortcut, an HTML page, an archive, a macro-enabled document
(`.docm`) — or any of them renamed to a listed name — is never handed over: "Brulion does not hand
“x” to other apps: only documents, spreadsheets, presentations, e-books, audio, video and text
files go out." A ZIP keeps its read-only text and nothing else.

C++: `src/app/pdf` (library `brulion_pdfview`): `PdfRenderPool` (at most two threads; priorities:
a visible page, a detail picture, a thumbnail, page sizes, search), `PdfReaderPool` (open readers
leased to one thread at a time, at most two idle per file and four in all, closed after 10 s idle —
Syncthing on Windows can replace the file —, forgotten when the file changes and before Brulion
itself writes, moves or deletes it or a folder holding it: `Workspace::releasePdfReaders`, as
Windows cannot replace, move or delete an open file; during a Nextcloud sync run, which may do the
same to any PDF, no reader waits idle at all — `PdfReaderPool::suspendIdle()` from the run's start
to its end, `WorkspaceSync`), `PdfThumbnails`,
`PdfPageLayout` (pure; rows of one or two pages), `PdfViewModel` (QML `PdfViewModel`: status,
error, locked, passwordAvailable, passwordRejected, unlocked, pageCount, scale, zoomPercent,
fitWidth, twoPages, coverAlone, contentWidth/Height, currentPage — the page under 40 % of the view's
height —, pages, detailRevision, search properties, selectionAvailable, selectedText,
selectionPage, selectionRevision, selectionAnchor, selectionPageRect, outline, linkRevision;
setViewport, settle, positionForPage, positionForTarget, pageAt, zoomAround, setScaleAround,
fitToWidth, pageUrl, detail (also the pages' rows' detailUrl / detailBox), hitRects, nextHit,
previousHit, clearSearch, reload, unlock, selectText, selectWord, selectAll, clearSelection,
selectionRects, copySelection, linkRects (rows with a `box` rect for the typed delegates),
outlineIndexForPage; `moveViewport(x, y)` moves the Flickable) and `PdfViewImageProvider`. The platform's side: `PdfReader::selectText`, `selectRange`,
`outline`, `links`, `open(file, info, password)` (`src/platform/pdfpages.h`) and the pure
`src/platform/pdftext.h`. Tests: `tests/app/pdf` (`tst_pdflayout`, `tst_pdfthumbnails`,
`tst_pdfviewmodel`), `tests/platform` (`tst_pdfpages` — also on `tests/data/pdf/spis.pdf`, a PDF
written by hand with an outline, links and an annotation —, `tst_pdftext`, `tst_externalfiles`),
`tests/app/shell/tst_workspace_pdf.cpp`, `--selftest-shell` (below), the Android emulator's `--pdf`
mode and `tests/web/pdf.spec.mjs` (docs/ANDROID.md, docs/WEB.md). Screenshots:
`tests/app/shell/screenshots.sh pdf-embed pdf-viewer pdf-zoom pdf-search pdf-jump pdf-locked`.

## Audio and video (`qml/media`, `src/app/media`)

A sound or a video in a note (docs/SYNTAX.md §2 "Audio and video": `![[Nagranie.m4a]]`,
`![](film.mp4#t=83)`) is a player in its block, and the file opened as a page is the player view —
never its bytes as text.

* **The player in a block** (`qml/media/MediaEmbedTile.qml`, BlockListModel's media embed
  `{media, kind, path, target, name, start, end}` — the file's URL is asked for only when it
  plays): a card with the file's name and the bar (`MediaControls.qml`): play / pause, "1:23 /
  3:42", the seek bar (`MediaSlider.qml`; a press on a file not loaded starts it there), the speed
  (a click: the next of 0.5× … 2×), mute and — where there is room, not on touch UIs — the volume,
  and **More** (`MediaEmbedMenu.qml`: Open in the player, Transcribe, Copy link to 1:23, Open in
  another app). A video shows its first frame (the poster, below) with a play button over it until
  it plays, then its picture (`MediaVideoSurface`: Qt Multimedia's `VideoOutput`); a click on the
  picture plays or pauses it, the bar's **Full screen** puts it over the window (`MediaFullscreen`,
  the window full screen too on the desktop; Escape, F or the button leave; Space, ← / → 5 s, M).
  The length shows before anything plays. A file this device cannot play says so ("Cannot be played
  here"). A time fragment starts the file there (the bar shows that time before it plays) and
  pauses it at its end. A click on a player's buttons or bars never takes the keyboard (Tab reaches
  them: `focusPolicy: Qt.TabFocus`) — pausing a recording does not stop the writing of a block.
* **One player** (`MediaPlayback`, a QML singleton over Qt Multimedia's QMediaPlayer): every embed,
  the player view and the now-playing bar steer the same one, so one file plays at a time —
  starting another pauses the one before, which keeps its place (positionOf: the next Play goes on
  from there; played to the end, from the start). Speed, volume and mute hold for every file.
* **Off screen** — the decision: a **sound plays on** when its block scrolls away, its page closes
  or the view changes (a lecture keeps playing while you write); the **now-playing bar**
  (`MediaNowPlaying.qml`, floating over the bottom of the window, above the bottom navigation and
  the keyboard) shows it while no view does: its name (a click opens the player view), the time,
  play / pause and close (unloads it, its place kept). A **video plays only where it is seen**: when
  the last surface showing it goes (its block scrolled away — the list deletes the delegate —, the
  view closed) it pauses; the surface attached last has the picture (the full screen over the
  embed, the player view over the journal), and the one before takes it back when that one goes.
* **The player view** (`qml/content/ContentMediaView.qml`, PageView's `mediaRoute`): the bar with
  the file's name and "Open in another app", the video large (or the sound's icon), the bar with
  ±10 s, the volume and full screen, and under it the notes that embed or link it (ContentBacklinks).
  A link with a time (`[[wykład.mp3#t=83]]`, PageController.activateLink: `route.mediaStart` /
  `mediaEnd`) starts it there. Keys: Space or K, ← / →, M, F.
* **Posters and lengths** (`MediaPlayback.info()`): read in the background, a file at a time, by a
  second player without sound (`MediaProber`); a video's first frame is kept as a PNG in the cache
  folder (`media-posters/`, keyed by the file's path, size and time), so a page opened again shows it
  at once.
* **Where nothing plays** (a build without Qt Multimedia — `BRULION_MULTIMEDIA` off, the browser):
  the card names the file with "Open in another app" (a new tab in the browser, whose own player
  plays it), the player view the same as an empty state; no "Voice note" command.

Qt Multimedia's backend: FFmpeg on Linux (the Nix package's FFmpeg is built for exactly the formats
above and the encoders of voice notes: nix/multimedia.nix), Windows and the AppImage (Qt's own
FFmpeg); on Android Qt's Android backend — the system's MediaPlayer and MediaRecorder, every format
Android plays — without the FFmpeg backend (docs/PERFORMANCE.md "Audio and video: what Qt Multimedia
costs").

## Voice notes (`qml/media/VoiceRecorderDialog.qml`, `src/app/media`)

**Record voice note** — the slash command **Voice note** (`/nagranie`, `/record`, `/dictate`…), the
microphone of the touch toolbar, the palette's "Record voice note" (uiAction `recordVoiceNote`) and
quick capture's **Voice note** — opens the recorder: it asks for the microphone
(`QMicrophonePermission`: Android's runtime request, the browser's, always granted on the desktop)
and records at once. The dialog (a bottom sheet on phones) shows the time recorded, a level meter of
the last seconds (48 bars, newest on the right; core `media::meterLevel`: the peak from −60 to
0 dB), Recording / Paused / Saving…, the words heard so far (below, "Text while recording"),
**Pause** / **Resume**, and the microphone's name. **Save** ends it; **Cancel**, Escape, the back gesture throw it away (its file deleted). A denied microphone or a
failure says why with **Try again**. It stops by itself after three hours.

* **Where it goes**: from the editor at the caret of the block being edited (a block of its own
  after it, or at the end of the page, when nothing is edited: PageController.insertRecording);
  from the palette off a note and from quick capture into a new block at the end of today's journal
  (AppController.captureRecording). The file moves from the cache folder into the notebook's
  attachments folder under its name (docs/SYNTAX.md §2 "Voice notes and transcripts").
* **The format** (`VoiceRecorder`, `voicerecorder_qt.cpp`): the samples come from QAudioSource — the
  meter and the time are the recorder's own — and go to QMediaRecorder through QAudioBufferInput as
  AAC in MPEG-4 (`.m4a`: plays on every phone, browser and computer, Obsidian on an iPhone too),
  else Opus in Ogg; where the backend has no buffer input (Qt's Android backend) the system's own
  recorder writes AAC from the microphone (`metered` false: the dialog shows a breathing microphone
  instead of the meter); where nothing encodes, a WAV file written here.
* **Transcripts** (`AppController.speech`, `Transcriber`; Settings → Extensions → **Speech**,
  `qml/media/MediaSpeechSettings.qml`): speech recognition on the device — OpenAI's Whisper, run by
  whisper.cpp over the same ggml as the local language models (core `speech`, the plugin
  `brulion_speech`, loaded the first time) — so no sound leaves the device. The settings: the model
  (Whisper Small 8-bit, 252 MB, the default — Polish good enough to edit; Base 8-bit, 78 MB — fast,
  rough in Polish; Large v3 Turbo 5-bit, 547 MB — the most accurate), its file (Download / Stop /
  Remove, from Hugging Face, pinned and checked as the language models' are), the language
  (Automatic, Polish, English) and **Transcribe voice notes** (on: a voice note just recorded is
  transcribed by itself when a model is here). Any recording in a note is transcribed from its
  menu (**More → Transcribe**). Recordings are heard one after another on a worker thread: decoded
  to 16 kHz mono (a WAV file read directly, anything else through QAudioDecoder), heard, and the text
  written right under the block that embeds the recording — the note as it is then (a change made
  meanwhile is kept; the editor showing it merges the write), as ordinary blocks to edit: a short
  recording (one paragraph) as one paragraph of text, a longer one as paragraphs of whole sentences
  — a new one after a pause of 2 s, or once one is half a minute or 600 characters long (core
  `media::transcriptParagraphs`) — each starting with a link to its moment, `[[Nagranie
  2026-11-20 18.30.m4a#t=83|1:23]]` (`[1:23](assets/Nagranie%201.m4a#t=83)` under a Markdown
  embed: docs/SYNTAX.md §2 "Voice notes and transcripts"). A click on such a link plays the
  recording's embed on the page from that moment (PageController.activateLink's `playMedia` →
  `MediaPlayback.playAt()`: whatever it played, paused or not, at the fragment's end or not), the page
  staying where it is; a link to a sound no block of the page embeds, and to a video, opens the
  player view there as before. While it
  works the player's card says so ("Reading the recording…", "Opening the speech model…",
  "Transcribing… 42 %") with **Stop**; a toast says when it is done or why not (no speech heard, the
  note no longer has the recording). The model stays in memory five minutes after the last one.
  The AI chat's dictation on the desktop hears its recordings with the same model
  (`Transcriber::transcribeFile()`: "AI chat" → Dictation). Absent in builds without the model on the
  device (`BRULION_LOCAL_AI` / `BRULION_SPEECH`).
* **Text while recording** (`LiveTranscript`, a QML type: the dialog's `voiceRecorderLive` under the
  state; dictation's text at the chat's caret; the setting **Text while recording**, `speech/live`,
  on by default): the recording so far heard again and again while it records, as whisper.cpp's
  stream example does (core `speech::LiveWindows`): the samples (`VoiceRecorder`'s sample listener:
  one channel of floats as the microphone gives them) make a window that is heard every 3 s as it
  grows — its text tentative, replaced by the next hearing — until it is 10 s long; then its text is
  kept but for a last segment that reaches its end (a word may be cut there: the next window starts
  at it), and a word repeated at the seam is dropped. Each window is heard on the speech worker
  (`Transcriber::hearLive()`: resampled to 16 kHz there, one greedy pass without fallbacks; never on
  the GUI thread), one at a time; a transcription waiting goes first. Whisper's loops (a phrase again
  and again: `speech::withoutLoops`) are said once. The dialog shows the end of the text (`tail`, 240
  characters, plain text), "Listening…" before the first words. An "auto" language is the one the
  first window with words was heard in (the next windows are heard in it); the dictation's is its
  own. It is a preview: the note's transcript (and the dictation's text) is the whole recording
  heard again when it ends, as before. **Too slow**: a window that comes back more than 10 s behind
  the recording, twice, ends it for that recording ("Text while recording stopped: this device hears
  it too slowly") and marks the model too slow on this device (`speech/liveSlow`,
  `Transcriber.liveSlow`): no text while recording with it until **Try again** in the settings
  (`speechLiveRetry`), where a note says so. None with the system's own recorder (Android without
  the FFmpeg backend: no samples pass through the app, `metered` false). Tests: `tst_media_live`,
  `tst_speech_live`, `tst_media_transcriber`, `tst_media_dictation`.
* **Tests without a microphone**: with `BRULION_TEST_MICROPHONE` set every recorder hears a made-up
  voice (a tone that swells and fades) — the shell self-test's voice note, screenshots.

## Scrolling performance

Long pages, the journal stream, Tasks and the calendar agenda are lazy `ListView`s: a row is
created when it scrolls into the list's cache and destroyed when it leaves, on the GUI thread,
while the list moves. Measured with `--scroll-probe` (see "Dev tools"); ctest
`tst_scroll_jumps_qml` fails on jumps (also of the scroll bar's thumb; the probe follows only the
rows the view placed, `tst_view_rows`), `tst_touch_flick` / `tst_touch_flick_compressed` on Qt's
touch faults, `tst_row_scroll` on the scroll bar, `tst_smooth_scroll` on the wheel.

A page of up to `BlockEditor.wholePageRows` rows (1000; 300 on phones and in the browser) is
**created whole**: its list's cache reaches past the page's end (`wholePageCache`), so the rows on
screen are created at once, as on any page, and the others in the background — ListView's
asynchronous cache, one row per incubation slice — and scrolling creates no rows and releases
none. The page stays whole until it has a quarter more rows than the threshold (no rows dropped
and created again while rows are typed or pasted at it); a longer page keeps a cache of about two
screens (1600 px). Whole only while the app is in use (`MemoryPolicy.inUse`, "Navigation" → Away):
away, the page keeps the rows near the view as a longer page does, and the others are made
again, in the background, when it is back. The price, measured on 2026-11-06 (docs/PERFORMANCE.md "Scrolling pages:
cheaper rows, pages created whole"): about 0.1–0.2 MB a row (a 1100-row outline 200 MB more), a
new width — the window, the side panel, a phone turned — lays every row out again (36 → 337 ms on
that outline), and the background filling makes some frames of 25–45 ms just after a page opens.
Rows are also made cheaper (below): the rows on screen, and every row of a longer page, are still
created while the page opens or scrolls.

The **linked references under a page** (`ContentBacklinks` in PageView, below the editor's
footer) follow the same rule: only what the reader can reach is made (docs/PERFORMANCE.md "L7").
PageView gives the section `visibleBottom` (how far into it the editor's viewport reaches) and a
`lookahead` of one screen; while the section is beyond that, `BacklinksModel` counts the
references (the heading's "40 linked references", the page tools) but renders one and lists none,
and their room is reserved (`reserved`: an estimate per reference, so the page's height and its
scroll bar hardly move). Once the view comes within a screen, the first references fill the part
on screen at once and the rest follows ten at a time (`revealStep`, 32 ms apart), each step only
while the listed ones end within reach; "Show N more" appears once `pageSize` are listed.
`revealAll()` lists them at once (route.open "unlinked" scrolls to the section's end). The side
panel's section and the picture's and PDF's references list everything at once (no
`visibleBottom`). Each reference renders its block as the editor holds it without parsing the
referring note: `BlockDocument::Locator` finds the block by its line from the index's
`ParsedNote` and parses that block alone.

### Rules for contributors

Any delegate of a scrolling list (a block and everything inside it, a day of the journal, a Tasks
or agenda row) follows these; the reasons are below.

1. **Final height at creation.** The height chain of a delegate is bindings, not Layouts or
   positioners (a `RowLayout`'s, a `Row`'s / `Column`'s / `Flow`'s size arrives with its next
   polish, after the list has placed the row). A sum over a Repeater's items reads `itemAt()` and
   a `revision` the Repeater bumps on `itemAdded` / `itemRemoved`. What a block shows from
   elsewhere is there when it is created: a model's first run in `componentComplete`
   (`QQmlParserStatus`, as `QueryResultModel`), not a `Qt.callLater` / `QTimer` later; a
   picture's size from its file's header (`picturesize.h`; a photo stored sideways with an EXIF
   orientation upright, as every `Image` showing the notebook's pictures has `autoTransform`), not
   from the loaded image — which is loaded only near the view ("Pictures hold their pixels only near the view" below).
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
   "Binding loop detected"). Make such a Loader inactive only once its popup has closed
   (`onClosed`, or a signal sent after it — `VoiceRecorderDialog.released`): a popup destroyed while
   it still closes never tells Qt's overlay it is gone (Qt 6.12), and the next menu to close
   anywhere in the window gives the keyboard to what had it when that popup opened — an editor long
   hidden — instead of to what had it before the menu.
5. **Cheap per row, on the GUI thread.** No file reads beyond a header, no parsing of whole
   documents, no icon theme lookups: an SVG's size comes from its root element only (Qt's SVG
   reader parses the whole drawing — 0.6 s for a 2.4 MB one on a Raspberry Pi 4). Bound what one
   row creates: a query block makes 20 result rows on touch screens, 50 with a mouse
   ("Show more" adds the rest). Text is laid out once: a rich-text `Text`'s height is never 0
   (`Math.max(1, implicitHeight)`), a `TextEdit` is not a `Loader`'s item itself (an `Item`
   around it), and nothing reads a wrapped text's `implicitWidth` ("Why: creating a row stalls
   the list").
6. **Every list scrolled by touch gets `TouchFlick.enabled: true`** (not for lists with a
   `snapMode` or `StrictlyEnforceRange`, where it does nothing).
7. **A long list whose rows differ in height uses `BRowScrollBar { list: … }`**, not the attached
   `BScrollBar` (its thumb jumps: "The scroll bar: `RowScroll`" below). A model that hides rows as
   zero-height delegates offers `visibleCount`, `visibleRowsBefore()` and `visibleRowAt()`.
8. **Never set `x` on a delegate's root: inset the row.** A vertical `ListView` places every
   delegate at x 0 (an `x` binding there holds only until the next layout — Tasks' rows hung up to
   200 px left of their centred header). The delegate is as wide as the list and the row is inset
   into its column: a control's `leftInset` / `rightInset` and paddings (`TasksRow.sideMargin`, All
   pages, Search), an inset its parts are drawn from (`ShellExtensionView.inset`: its lines and
   its header place their content at `inset`) or a full-width `Item`
   with the row inside it (the agenda's days). A header that offsets its column counts the
   offset in its height.
9. **Measure, before and after:** `tests/app/perf/scroll-probe.sh` (also `CACHE=0`: every row
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
task box's icon for "done" / "deferred" only; `BIcon` creates no image without a name or before
it is first visible (Qt's `IconImage` without a source searched the icon theme on disk);
`BFocusRing` (`src/app/ui/focusring.h`) makes its ring (`BFocusRingShape.qml`) the first time it
shows; a button's spinner exists while it is busy. A block's optional parts and the views of its
non-text blocks are files of their own, `qml/editor/BlockPart<name>.qml`, whose components the
C++ singleton `BlockParts` (`blockviews.h`) makes once per engine (`sourceComponent:
BlockParts.part("Bullet")`): a component declared in the delegate is an object of every block.
Made in C++, they have no creation context, so the Loader creates the part in the block's own
context and its bindings end with the block; a part finds its block once,
`BlockViews.ancestor(part, 2)` (the Loader, then the block), and its texts keep BlockDelegate's
translation context (`qsTranslate`). The shell
does the same (docs/PERFORMANCE.md "L10"): its dialogs and menus, the journal's "Go to day", the
side panel and the update banner are made the first time they open or show — the four popups
used most are built ahead in the background after the notebook is ready —, a phone's drawer
content 0.6 s after the notebook is ready (asynchronously) or when the drawer opens. The editor's palette, locale and
clock reach its controller before its rows are created (applied later, they made every row
render twice).

Text layout is most of a block's creation, so a block lays its text out once:

* a rich-text `Text` has the height `Math.max(1, implicitHeight)` (`BlockText`, `BRichText`): left
  to its implicit height, its height grew from 0 after its first layout, and
  `QQuickText::geometryChange` (Qt 6.11) lays a wrapped rich text out again whenever its height
  grows from 0;
* a `TextEdit` loaded by a `Loader` sits in an `Item` of its own (`BlockText`'s `TintedText`,
  `BRichText`'s `RichBox`): a Loader reads its item's implicit width, and a wrapped TextEdit
  computes that by laying its text out unwrapped and then wrapped again (two more layouts);
* the rounded shapes of tags, chips, inline code and highlights (`BTintShapes`) and a page's icon
  in front of a link to it are two Repeaters (`RichTextPills.plainShapes`, `iconShapes`): only an
  icon makes a `BIcon`, every pill carried one before.

The editor's scroll bar (`RowScroll`, below) runs every frame while the list moves: it looks the
list's properties and methods up once per list and model, not by name every frame.

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

### Rendered text without rich text: `RenderedTextItem` (Z10 stage A)

A block's rendered text costs most of a row: a rich-text `Text` (or, with tints, a read-only
`TextEdit` with `RichTextPills` and `BTintShapes`' Repeaters) holds a `QTextDocument` parsed from the
renderer's HTML and laid out by `QTextDocumentLayout`. Stage A of the editor's rewrite
(docs/PERFORMANCE.md Z10) draws it with one C++ item instead, behind a switch: **`BRULION_TEXT_ITEM=1`**
(`EditorInput.textItem`; off by default — nothing changes without it).

* **Runs, not HTML** (`src/core/markdown/renderedtext.h`). `InlineRenderer::renderRuns()` /
  `renderBlockRuns()` give `RenderedText`: the text Qt's rich text shows (entities decoded, `<br/>`
  as `\n`, white space collapsed as Qt's HTML import collapses it), the tag pairs as nested
  `elements`, flat `runs` (bold, italic, strike, code, sup/sub, underline; colours; a `Pill` or `Box`
  background by RichTextPills' rule; the link; a page's app icon), `objects` (icons, rules) and a
  display ↔ source map (`renderTraced()`: every piece of the HTML with its source range — verbatim
  text character by character, a construct as a whole). It is the renderer's own HTML taken apart,
  so the HTML and the runs never disagree: `runsToHtml()` gives the HTML back byte for byte, which
  `tst_markdown_renderparity_*` checks for every rendering the renderer's tests make (the tests
  built again with a render observer). HTML that is not well formed (a `<mark>` pair across
  emphasis: `<mark>a **b</mark> c**`) is not `valid`; a formula with fonts of its own
  (`\mathtt`, `\mathsf`, `\overline`), a rule and preformatted text are not `drawable`.
* **The model** gives it as the role `renderedText` (`BlockRowData.renderedText`, the value type
  `renderedText`: a shared pointer, `drawable`, `text`), rendered only when read and kept while the
  row's `html` is (the same rendering through the traced path; the task chips are appended, a list
  item's `# heading` lines put together as for `html`). Source positions are the block body's.
* **The item** (`src/app/editor/renderedtextitem.h`): one `QTextLayout` with a format per run, laid
  out synchronously when the text, width, font, line height, alignment or line limit changes — so
  its `implicitHeight` is final when the row is created (rule 1 above) — and never for anything
  else; a new width keeps left-aligned lines that no width broke (as QQuickText does: a window
  resized lays out only the wrapped blocks). Lines sit where `QTextDocumentLayout`'s `FixedHeight` puts them (the baseline at 4/5 of the
  fixed line, in 26.6 fixed point), so rendered and edited blocks keep their lines; the text is one
  `QSGTextNode` (`QQuickWindow::createTextNode()`, `addTextLayout()`: the graph's way, L16). Chips,
  tags, code and highlights are one geometry node of antialiased rounded rectangles under the text
  (with the software renderer, one painted image); the hovered link's underline and the find bar's
  matches are geometry over the same layout, not a new one. A page's app icon keeps its three
  no-break spaces; `iconShapes` gives their place and a Repeater of `BIcon` draws it (rare).
  `linkAt(x, y)`, `positionAt(x, y)` (a source position), `rectForSource(from, to)`,
  `maximumLineCount` with "…" (`truncated`) as `RichTextPills` cuts the lists' rich text.
* **BlockText** shows it in the same Loader that holds the tinted `TextEdit` (no new object per
  row), for any block whose runs are `drawable`; everything else — and every block while a screen
  reader is on (`DeviceInfo.screenReader`: `BLinkTargets` makes the rich text's links accessible
  items) — keeps the rich-text path. The item is `StaticText` with the block's plain text as its
  accessible name. Clicks, menus, hover, previews and the find bar go through the same
  `BlockText` functions (`linkAt()`, `renderedItem`).

`tst_editor_renderedtext` compares the item with the rich-text path on the same HTML: the lines'
breaks, widths and positions with a `QTextDocument` of the fixed line height, the shapes with
`RichTextPills`' rectangles (within half a pixel), the shown text with `toRawText()`; and its
layouts, links, source positions, the cut, Polish no-break spaces and emoji. Status and numbers:
docs/PERFORMANCE.md "Z10 stage A: rendered text from runs".

### One item per page (Z10 stage C)

The goal is Zed's rule 4 (docs/PERFORMANCE.md "Only what is on screen, only what changed"): the
editor is one element that lays out and draws only the lines on screen. Today a page is a
`ListView` of `BlockDelegate`s (some 45 QML objects a row, its text a `RenderedTextItem` or a rich
`Text`); a page of up to 1000 rows is created whole, so a new width lays every row out (337 ms on
an 1100-row outline), and the list's content height is an estimate from the rows it created, so
the scroll bar (`RowScroll` counts rows to hide it) and "scroll to block" (`positionViewAtIndex`,
then the row's own geometry, twice) are approximate on long pages. Stage C replaces the list with
one C++ item, `PageItem`, that draws the rows it can and hosts the others as QML delegates. This
is the design; what exists of it is listed at the end ("State").

**Shape.** A plain `Flickable` (kinetic scrolling, `TouchFlick`, `SmoothScroll`, the scroll bar,
the keyboard insets — everything that works on Flickables today) whose `contentHeight` is the
page's height from the height sum tree; `PageItem` fills the Flickable's viewport, not its
content, and draws the rows that meet the viewport and a margin of half a screen above and below
it (`viewportY`, `viewportHeight` bound to the Flickable). The header (title, properties draft,
zoom breadcrumb) and the footer stay QML items above and below the rows, in the Flickable's
content, their heights part of the total.

**Rows: measured, cached, laid out lazily.** Each row has an entry in a cache keyed by its
uid (`BlockListModel` keeps a block's uid through every edit, undo and remote change):

* what the item draws for it — the text as a `RenderedTextLayout` (the layout and drawing code of
  `RenderedTextItem`, shared: one `QTextLayout` from the row's `renderedText` runs, the tints'
  shapes, the hovered link and the find matches), and the row's decorations computed from the
  same geometry as `BlockDelegate`'s (indent, bullet, number, task box, guide lines, top and
  bottom padding by `prevKind`, the heading sizes);
* its measured height at the width it was laid out with.

An entry is made when its row comes within the margin (in `updatePolish()`, before the frame:
the rows on screen always have their final height in the frame that shows them — rule 1 above,
without a delegate) and dropped least-recently-drawn first beyond a bound (three screens of rows
plus 256); `dataChanged` drops the parts its roles touch (`renderedText`/`html` the text,
`depth`/`prevKind`/`view` the geometry, `isSelected`/`selectionEdge` nothing but the band), a new
width lays out again only the rows that break differently (`RenderedTextItem`'s rule: a
left-aligned row no width broke keeps its layout), a new font or theme every entry. A row never
laid out has an **estimate**: its kind's paddings plus its plain text's length × the font's
average advance ÷ the text width, in whole lines (no layout, no font shaping); a hosted row its
last measured height, else its view's typical height. While nothing moves, an idle measurer lays
out the rest of the page in slices of at most 2 ms a frame and stops at any input, so the total
— and with it the scroll bar — becomes exact on pages that are read for a while; nothing waits
for it.

**The height sum tree (`RowHeightTree`, `src/app/editor/rowheighttree.h`).** One entry per model
row (hidden rows — folds, a zoom — have height 0), each with a flag *measured* or *estimated*;
heights are kept in 1/64 px fixed point, so sums are exact and a long session of updates never
drifts. A Fenwick tree over them gives the offset of a row and the row at an offset in O(log n), a
height set in O(log n); inserting, removing and moving ranges of rows (the model's row signals)
and setting all heights at once rebuild it in O(n) — a memmove and a linear rebuild, far below
what the model's own row signals cost (docs/PERFORMANCE.md "Z10 stage C"). (Zed's `SumTree` is a B-tree with O(log n) splices; worth it only for pages of millions of
rows.) The tree also counts the measured rows and their heights, for the estimate of what is left
and for tests.

**Scroll anchoring (`ScrollAnchor`, `src/app/editor/scrollanchor.h`).** The scroll position is not
a pixel offset but an **anchor**: a row (followed through the model's row signals, i.e. a block
uid) and an offset in pixels from that row's top to the viewport's top edge. The view's y is
derived — `offset(anchor row) + anchor offset`, clamped to the content — so every change of a
height above the anchor (an estimate replaced by a measurement, a row typed into above, a picture
whose size arrives) moves the content's coordinates, never what the user sees. The rules:

* the user scrolls (`scrollTo`, `scrollBy`): the anchor becomes the row at the new top edge and the
  offset into it;
* a row above or below the anchor changes height, rows are inserted or removed elsewhere: nothing
  on screen moves (rows below the anchor's row move down or up from it, as text below a growing
  paragraph does);
* the anchor's own row changes height: its offset stays (its top stays put; text added at its end
  grows downwards), clamped to the new height; when the height changes because the width changed
  (a reflow) the offset keeps its fraction of the row instead, the closest one can stay to the same
  line without a line-level anchor;
* the anchor's row is removed: the anchor becomes the row that takes its place, keeping its
  offset, when rows are inserted at exactly that place in the same change (a remote edit of that
  block: `replaceContent()` removes the block and inserts its new version), else the next row at
  offset 0 (the content after what was removed moves up to the top edge);
* the anchor's row is hidden (a fold above it, a zoom): the next shown row at offset 0 — the same y,
  the fold point; unfolding again grows rows above the anchor, so the row at the top stays;
* rows moved (Alt+↑/↓, a drag): the anchor moves with its row;
* the content ends: the y is clamped to [0, total − viewport]; reaching an end re-anchors there;
  the top is sticky — a view at y 0 is anchored on row 0 (the header, also before it has a
  height), so a title that wraps again or rows inserted at the top of the page keep the top in
  view, as `ListView`'s header does today;
* "scroll to block" (`positionAtRow(row, mode)`): the anchor becomes the target row itself with
  the offset the mode needs (Beginning 0, Center its height / 2 − viewport / 2, End its height −
  viewport, Contain the least move), so measuring the rows between the old position and the
  target in the next polish moves nothing: the block is exactly where it was asked to be, in one
  step, whatever the estimates said.

The Flickable is told the derived y as its `contentY` only while it stands still: setting
`contentY` stops a running flick (`QQuickFlickable::setContentY` resets its timeline), so while it
moves the difference is kept as a *slack* between the Flickable's `contentY` and the item's y and
folded into `contentY` when the movement ends — what `ListView` does with `originY`. The scroll
bar shows `y / (total − viewport)` with its size frozen while the view moves (as `RowScroll`);
`RowScroll`'s row counting is not needed for this view.

**The edited row: a QML delegate over the item.** Editing keeps everything it has today (the
`InlineEditor`, the highlighter, completions, the input method, the touch toolbar): the edited
row is a real `BlockDelegate`, hosted by the item, placed over the row's place, and the item does
not draw that row. The host creates it from the editor's delegate `Component` with the row's
roles as initial (required) properties and `index`, and gives it a model object of its own (a
`QQmlPropertyMap` of the roles: what `BlockRowData` reads), updated from `dataChanged` and the row
signals as `QQmlDelegateModel` updates a ListView's delegate; its height is the row's measured
height (the tree follows it: the rows below move, the anchor keeps the view). It stays hosted
while the row is edited or holds the keyboard, also when scrolled out of the margin (a key brings
it back, as today), and is released when editing ends; the row is then drawn from the cache again,
which is laid out from the row's new `renderedText`.

**Rich blocks: hosted delegates.** Code, calc, math, Mermaid, charts, tables, queries, callouts
and quotes (their frames), rules, the frontmatter, rows with pictures, PDFs or embedded pages,
and task rows until the item draws a task box, are hosted the same way, while their rows meet the
viewport and its margin; a small pool per view kind reuses them (as `ListView`'s `reuseItems`, but
only for the same view, so a row never changes its kind of content). A hosted delegate is made
by `hostedrow.h` (the row's roles as required properties, `index`, a model object of its own) —
shared with the Atrament page, which hosts its foreign rows the same way (docs/ATRAMENT.md "Rows
Atrament does not draw yet"). Their heights are their delegates' and they keep the delegate rules above (final height at creation, no height change
while only scrolling, create only what is shown); a hosted row that grows moves the rows below it,
never the view. The item draws the text rows — paragraphs, headings, list items, the bullets,
numbers, guide lines, the selection band and the flash —, which are 90–98 % of the rows of the
bench pages.

**Contracts** (what the rest of the editor relies on; today they go through `list`):

* *Rows by position*: `rowAt(x, y)` (the tree, then the row's geometry: −1 in a gap), `rowRect(row)`
  (in the item's coordinates; estimated rows say so), `rowItem(row)` (the hosted delegate or
  null). `BlockEditor.rowAt()`, the drag tracker, the drop hint and the context menus use these.
* *Selection*: the model's `selectionEdge` draws the band (geometry nodes, the same shape as
  `BlockDelegate`'s band); a mouse drag selects through `rowAt()` with the Flickable scrolled at
  the edges, as now; Shift+click and the touch grips call the same controller functions; the
  grips are two QML items placed at the band's ends.
* *Drag and drop*: the drop target is `rowAt()` of the drop point and the half of the row it is in;
  the drop hint a QML item at `rowRect()`.
* *Keyboard navigation*: unchanged — `PageController` moves editing and the selection by row; the
  editor asks for the row to be shown (`ensureRowVisible(row)` = `positionAtRow(row, Contain)`)
  instead of `positionViewAtIndex`; the caret's own visibility is the hosted editor's.
* *Accessibility*: the item is `Accessible.List`; its rows, links and their actions need a
  `QAccessibleInterface` of its own (children per drawn row with their rectangles and plain text,
  `QAccessible::installFactory`). Until that exists, the editor keeps the `ListView` while a screen
  reader is on (`DeviceInfo.screenReader`), as `BlockText` keeps the rich text.
* *Find in page*: the matches are the rows' overlays (`findQuery` and its options, as
  `RenderedTextItem`); the current match's row is shown with `positionAtRow(row, Center)` and
  flashed; an editable match hosts its row as the edited row.
* *Scroll to block / reveal*: `positionAtRow(row, Center)` then the flash, in the same frame.
* *Self-tests and probes*: `rowItem(row)` gives the delegate of a hosted row; the drawn rows are
  read through `firstShownRow` / `lastShownRow`, `rowRect()`, `rowText(row)`, `rowLineCount(row)`
  and `linkAt(x, y)`; the bench probe counts `drawnRowCount` where it counts `BlockDelegate`s.
  Self-tests that inspect a delegate's insides (`BlockText`, `TaskBox`) host their row first
  (`hostRow(row)`).

**Stage B, later: the edited row last.** Stage C keeps the editing in one `TextEdit` per edited
block, so a text selection cannot cross blocks (the block selection does). Stage B draws the
edited row too (its source with the highlighter's formats in a `RenderedTextLayout`), keeps the
caret and a selection of source ranges across rows in C++, and uses a hidden `TextEdit` only for
the input method's composition and the platform's text services; copying, Shift+arrows and drags
then select text across blocks as in a document editor.

**Migration and risks.** Behind `BRULION_PAGE_ITEM=1` (`EditorInput.pageItem`), off by default;
pages only (a journal day keeps its list: stage A2 makes the day one item):

1. foundations: the tree, the anchor, the shared layout, the item drawing text rows read-only
   with rich rows hosted (this stage, partly done: "State");
2. every `list.*` use in `BlockEditor` and the self-tests behind the row contracts above, which
   both the `ListView` and the item answer — the switch then is one property;
3. editing through the hosted delegate, the selection, drag and drop, find, reveal;
4. accessibility; then the item by default and the `ListView` path removed.

Risks: the Flickable's slack (a movement that ends at an end of a page whose estimates were off
lands a few pixels from the true end until it is folded); hosted delegates read `ListView`
attached properties (`ListView.view` gives `BlockDelegate` its width: the host gives the width
instead); focus and the input method in a hosted editor that scrolls out of the margin; the touch
gestures (long press, the selection mode, grips) reimplemented on one item instead of per row; the
title drawn above the properties (`titleFirst`: a display order that differs from the model's, a
permutation in the tree's coordinates); right-to-left text and centred rows in the shared layout;
memory, which falls (no per-row QML) only once the item draws most rows.

**State (2026-11-18): foundations, behind `BRULION_PAGE_ITEM=1`, off by default.**

* `RowHeightTree`, `ScrollAnchor` and `PageRows` (`src/app/editor/rowheighttree.h`,
  `scrollanchor.h`, `pagerows.h`; pure QtCore) as designed above: the tree, the anchor with every
  rule of the list, and the two kept in step with a model's row signals — a header row, the
  model's rows, a footer row; rows hidden by the model's `visible` role are 0 px (measured), shown
  again with an estimate. `tst_editor_rowheights`: the tree against a naive list (random edits of
  every kind, six seeds, exact sums after 100 000 updates), each anchor rule, random changes
  followed by uid, and `PageRows` on a list model and on `BlockListModel` with another device's
  changes merged in (`replaceContent`: the anchor's block kept, changed — adopted at the same
  offset —, removed) and folds; `bench_rowheights` the costs.
* `RenderedTextLayout` (in `renderedtextitem.h` / `.cpp`): `RenderedTextItem`'s layout, shapes,
  overlays, hit tests and scene-graph nodes, taken out of the item with its code where it was
  (the item derives from it; its QML API and behaviour are unchanged: `tst_editor_renderedtext`).
* `PageItem` (`src/app/editor/pageitem.h`) and `PageItemView.qml`: with the switch on, a page (not
  a journal day; not with a screen reader) shows its rows through the item over a Flickable; the
  `ListView` stays without rows and keeps the keyboard. The item draws paragraphs, headings and
  list items (their text, bullets, a document's numbers, guide lines) and hosts every other row
  as the editor's own `BlockDelegate` (code, tables, tasks, frames, pictures, embedded pages, rows
  tinted by a styled tag, selected, folded and edited rows): tapping a drawn row edits it — the row is then hosted and
  its inline editor works as in the list, the caret kept in view (`BlockEditor.ensureVisible()`
  through `PageItem.ensureVisible()`) —, links open, hover underlines them, the find bar's
  matches are drawn, "scroll to block", find and reveal go through `positionAtRow()` (a revealed
  row is selected, so hosted, and flashes; a selection's band is its delegates'). A hosted
  delegate stays while its row is within one and a half screens (a new width or a scroll over the
  margin's edge would make the same delegates again). The slack keeps flings running while rows
  above are measured. `tst_editor_pageitem` checks the item offscreen:
  rows laid out only near the view (also after a width change), the Flickable driven, the anchor
  through edits and measurements and while a fling runs, exact positioning on estimated rows,
  hosted rows with their roles and index, folds, links, a frame, and `RenderedTextLayout` against
  `RenderedTextItem`. The numbers: docs/PERFORMANCE.md "Z10 stage C: one item per page, the
  foundations".
* Not yet (the next steps, in the order of the migration above): task boxes drawn by the item (a
  task row is hosted now: most of what a new width still costs on a long outline), fold arrows and
  the bullet's hover halo of drawn rows (a folded row is hosted, so folding back works; folding a
  drawn row takes its block menu or Ctrl+.), drawing the selection's band and the flash itself
  (selected rows are hosted: a large selection hosts many delegates), drag-selecting across rows,
  drop targets, the touch toolbar's room at the bottom edge, the page's linked references under the
  rows (PageView hides them with the switch on), the idle measurer (the total, and so the scroll bar
  — the Flickable's attached `BScrollBar` — is an estimate until the rows were shown once), the
  bar's size frozen while the view moves, a pool of hosted delegates, accessibility, and the
  self-tests run with the switch on.

### A journal day as one item: `JournalDayItem` (Z10 stage A2)

A day of the journal stream is one `ContentJournalDay` with its own `BlockEditor`, and the editor
makes a `BlockDelegate` (some 45 objects) for every block when the day is created — most of a day's
cost. With **`BRULION_JOURNAL_DAY_ITEM=1`** (`EditorInput.journalDayItem`, off by default; it turns
`EditorInput.textItem` on too, so that the rows a day switches to lay their text out with the same
code) a day that is not being edited is drawn by one C++ item instead
(`src/app/editor/journaldayitem.h`):

* **The editor stays, its rows do not** (`BlockEditor.rowsDrawn`: the list has no model, is hidden
  and takes no input; its drop area and drag tracker are off). The day's controller, find bar,
  stream selection and every function of the editor are there as before, and the item takes the
  editor's geometry from it (`contentX`, `gutter`, `headingSize()`, `firstLineCenter()` of its line
  probes, …): only BlockDelegate's own formulas (paddings by the block before, the marker columns,
  the task box, the "Add date" room, the content's place and width) are repeated in C++.
* **What it draws**: every shown block of the views `text`, `heading`, `quote` and `rule` — the guide
  lines, the line from a parent's bullet to its children, fold arrows (on hover; always when folded
  or on touch UIs), bullets with their halo, task boxes of every status, a document's list numbers,
  headings, a quote's bar, a rule's line — and the block's rendered text, one `RenderedTextItem`
  per block made from C++ (no QML object per block; a page's app icons in the text as sprites). Bars
  and lines are rectangle nodes, the round shapes and icons sprites (`spriteImage()`: painted once
  per key and device pixel ratio, textures from the scene graph's atlas), so the software renderer
  draws them too. Its rows are BlockDelegate's to the pixel: `tst_content_journals_qml_dayitem`
  compares every row's top, height, first line and text with the editor's rows the day switches to.
* **A day it does not draw shows its editor's rows, the whole day** (`drawable` false): a table,
  code, a calculation, a query, a formula, a diagram, a chart, a picture or an embedded page, the
  page properties, a callout, raw HTML, text it cannot draw (`RenderedText::drawable`), a block
  tinted by a styled tag (`b.app.tagStyle`: "Styles of blocks by tag"). Hosting such
  blocks as QML delegates placed by the item would need the delegates anyway: every block view reads
  its `BlockDelegate` and the editor through it (`block.editor.*`). That is stage C's work (one item
  per page with a height sum tree, rich blocks hosted); for a journal the whole day is the simpler
  correct unit.
* **The rows are made when something needs them** (`ContentJournalDay.materialize()`, synchronous —
  the list's `forceLayout()`: every row in the same event, with the heights the item had, so nothing
  moves): a mouse press on the day (a drag from it selects blocks, as on the rows), a finger's tap or
  long press (a finger's press alone does not: most presses on a phone scroll the stream), a
  right-click, a middle click, files dragged over it; editing, a selection (also the part of a
  selection over several days), a zoom, a conflict, a title above the properties; the day becoming
  the stream's active day (today when the stream opens: the stream's commands act on it), the end
  of a selection moving into it, `begin()`, a screen reader. The day keeps its rows from then on.
* **Input goes on to the editor's own functions.** The item says what is under a point (`partAt()`:
  text, the "Add date" button, task box, number, bullet, fold arrow, the strip under the blocks) and
  the day calls what that part of a row calls — a text's `blockTappedWith()`, `openDateDialogAt()`,
  `activateLink()` or `editAt()` with the point in the new row's text (the caret lands where the
  finger was), the bullet's zoom, the box's `toggleTask()`, the arrow's `toggleFold()`, the menus
  (`openBlockMenu()` at the marker, `openTaskStatusMenu()`, `openDateMenu()` / `openBlockLinkMenu()`,
  `openBlockMenuAt()` at the pointer), `openLinkInTab()`, `longPressText()`. A press that made the
  rows keeps the item (drawing nothing: `opacity` 0) until it ends, and its moves go on to the
  editor's drag tracker (`dragPressed()` / `dragMoved()` / `dragReleased()`). Taps on a task box or a
  fold arrow change the page without making the rows.
* **Hover is drawn by the item** (`setHover()`): the row's fold arrow, a bullet's halo, a task box's
  tint, the link under the pointer underlined in its row only, the cursor, link previews
  (`LinkPreviews.hover()` with the item); an open task's "Add date" is a real `BIconButton` placed on
  the hovered row (one per day, while hovered), whose click opens the menu at the row's own button.
* **Accessibility.** With a screen reader on (`DeviceInfo.screenReader`) days are never drawn: the
  editor's rows make each block an accessible item and its links accessible items of their own
  (`BLinkTargets`). Qt Quick gives items created from C++ no accessible role or name without its
  private API (`QQuickAccessibleAttached`); the drawn item is a `List` named by the day's date.
* Left as they are: the fold arrow turns without its animation, a task box's colour changes at once,
  a quote's bar has square ends (a 1.5 px radius in the row), the round shapes are pictures at
  fractional positions (antialiased a little differently).

`tst_editor_journalday` checks the item alone (which days it draws, the rows' geometry by the
formulas, parts and links under points, hover, a fold, a task and another device's change followed
without laying unchanged texts out again, the sprites, a frame); `tst_content_journals_qml_dayitem`
and `tst_content_journals_span_qml(_touch)_dayitem` run the journal's self-tests with it (real mouse
and touch input through the drawn days). Numbers: docs/PERFORMANCE.md "Z10 stage A2: a journal day
as one item".

### The journal stream drawn by Atrament: one list, no item per day

With the editor drawn by Atrament (Settings → Editor → "Draw the editor with Atrament",
`AppSettings.editorAtrament`; `--atrament-editor` for the self-tests) the stream is one Atrament list
instead of the `ListView` of `ContentJournalDay` items (docs/ATRAMENT.md "The journal stream as one
list"; `qml/editor/EditorAtramentJournal.qml`, loaded by JournalsView by its file name):

* **A row per day**: the line between days, the date (today larger; "Today" / "Yesterday" as a badge;
  phones: the weekday and the word as a small line above the date), "Open as page" (phones: its
  icon), then the day's blocks — drawn by Atrament as a page's are — or, for a day without a note,
  the prompt whose tap begins the day from the journal template. A tap on the date or "Open as page"
  opens the day as a page; Ctrl+E opens it in source mode.
* **Only the days near the view exist**: their PageControllers are made a screen before they scroll
  in and go two screens after they left (they save as they go), except the day written in, one being
  edited, with blocks selected or zoomed into. A day above the view measured later moves nothing on
  screen (the list's anchor is a day and an offset into it).
* **One editor for the stream** (JournalsView's `streamEditor`, a BlockEditor without rows of its
  own): its controller is the day being written in — the day a tap, a key, a menu or a selection
  is in becomes that day first, the day before stops editing (unless a selection spans several
  days). The touch toolbar docks on the stream's frame as before; the side panel, the find bar's
  current match, Vim, Back's zoom and the stream's commands act on that day.
* **Across days**: ↓ on the last line of a day's last block edits the next day's first block (the
  caret at the same x), ↑ on the first line of its first block the day above's last one — days
  without blocks are passed (the QML stream stops at a day's edge); Enter at a day's end makes a
  block in that day, Backspace at a day's start merges nothing into the day above. Shift+↑/↓ past
  a day's edge and a mouse drag from one day into another select blocks of several days
  (StreamSelection, as above); the find bar tints its matches in every day.
* **A day zoomed into a block** shows the zoom's breadcrumb above its blocks (ZoomBreadcrumb, as
  ContentJournalDay's editor does: the page, the block's ancestors, the block; a crumb zooms out to
  it, the arrow one level up, Back too), made for that day while the zoom lasts; the zoom brings
  the day's heading, the breadcrumb and the block into view (the day got shorter).
* **Kept as QML**: the missed reminders, the days' count and "Go to day…" above the days, the empty
  journal's hint below them, a day's rows Atrament does not draw yet (its BlockDelegates, as on a
  page), a zoomed day's breadcrumb — placed over the list.
* Not yet: the selection's drag scrolling the stream at its edges (the wheel and Shift+click reach
  farther days), a long day laid out only where it shows (a day is laid out whole, as
  ContentJournalDay builds all its blocks).

### The task list drawn by Atrament

With Settings → Editor → "Draw the task list and search results with Atrament" (`AppSettings.viewsAtrament`,
experimental, where Atrament is built; `--atrament-views` for the self-tests) the Tasks view's
sections and tasks are rows of one Atrament list (`qml/planning/TasksAtramentList.qml` over
`src/app/views/atrament/tasksview.h`, docs/ATRAMENT.md "The views in Atrament (M3)") instead of the
`ListView` of `TasksRow` delegates; the quick-add field, the filters and the summary stay above it.
A row looks and acts as `TasksRow`: a click on the box ticks the task off (the undo toast), a
right-click on the row or its box opens its status menu, on a date, reminder, recurrence or
priority chip its token's menu, on a link its menu; a click on a `#tag` filters, on another link
follows it, on the row opens the task's page at its line, on a header folds its section, on the
subtasks' chip folds them; ↓ / ↑ move through the tasks (headers skipped), Space ticks the current
one off, Enter opens it — the task now at its place becomes current when it moves into Done.

The same setting draws the Search view's results (`qml/content/SearchAtramentList.qml` over
`src/app/views/atrament/searchview.h`) in its `ListView`'s place; the field, the recent searches and
the empty state stay QML. A result looks as SearchView's delegate: the kind's icon, the title (a
daily note's date in words) and the excerpt with the matched characters in the accent and bold, the
folder, "Close in meaning", an attachment's page, slide or part ("Page 3 of 12 · 2 more pages
match") and "Recognized text". The field keeps the keyboard: ↓ / ↑ move the current result (shown
highlighted and scrolled into view), Enter opens it; a click opens a result (Ctrl+click, a middle
click: in a new tab).

### Pictures hold their pixels only near the view

A page created whole has every picture's frame from the start. The frame is cheap; the picture
in it is not: a phone's photo (12 MP) shown 560 × 420 was decoded at 2048 px a side — 12.6 MB of
pixels — and drawn from a texture with mipmaps, 16.8 MB more, and every picture of the page at
once, so a page of forty photos held over a gigabyte, most of it never looked at. Three things
keep that down (numbers: docs/PERFORMANCE.md "Pictures and memory"):

* **`NearView`** (attached: `NearView.near`, `NearView.enabled`; `src/app/ui/nearview.h`) tells an
  item whether it is in its window's view or within reach of it. A picture's frame (`EmbedView`'s
  `embedPicture`) hands its `Image` a source only while it is near, so a picture is decoded when
  its frame comes within **one window height** (or width) of the window and lets go beyond
  **two** — farther than it entered, so that reading back and forth around one place decodes
  nothing again; a window lower than 480 px reaches as far as one of 480. Hidden — its view kept
  in the background, the window minimised, the app in the background — it lets go after 30 s (a
  look at another view and back shows the pictures at once). The frames of a window are looked at
  once per frame it draws (`QQuickWindow::afterAnimating`: after the polish, on the GUI thread), by
  their rectangle in the window, whatever scrolls them — a page, a day of the journal stream, a
  split pane; nothing runs while nothing is drawn, except one timer for the hidden ones. Loading
  is asynchronous (the decode never runs on the GUI thread), so a picture scrolled to faster than
  it decodes shows its empty frame for a moment. `BRULION_NEAR_VIEW=0` makes every item near (A/B
  measurements).
* **`PictureDecode`** (singleton; `src/app/ui/picturedecode.h`) gives the side a raster picture is
  decoded at: the longer side of its frame in device pixels, rounded up to a step (64 px up to
  512, 128 px above; at most 2048), as `Image.sourceSize` of both sides — Qt fits the file into
  that square with its aspect ratio and scales nothing up. A frame shows its picture at most a
  quarter smaller than decoded, so it needs no mipmaps; a window being resized decodes a picture
  again only at a step, and the one shown stays until the new one is there
  (`Image.retainWhileLoading` — only for the same file: `EmbedView` hands the source over by hand,
  `hold()`, so that another file's picture is never drawn in this one's frame). The picture view
  (`ContentPictureView`) decodes the fitted picture the same way and its own pixels ("Zoom to
  actual size") at up to 2048 px a side. An SVG is rendered into its largest box as before.
* **`MemoryKeeper`** (`src/app/ui/memorykeeper.h`; `main()` makes it) gives freed memory back to
  the system: pictures out of reach, a page left, the index built, the app in the background each
  tell it (`released()`), and once nothing was let go for 2 s (at the latest 10 s after the first)
  it asks the allocator on a worker thread (`platform::releaseFreeMemory()`: glibc's
  `malloc_trim`; nothing on Windows and in the browser) — at once when the app leaves the
  foreground, which is also the only time Android's allocator is asked to purge (it gives freed
  pages back by itself). Without it glibc kept what was freed: the process stayed as large as at
  its peak for as long as it ran (docs/ARCHITECTURE.md "Process memory").
  `BRULION_MEMORY_KEEPER=0` switches the giving back off (A/B).
* **`MemoryPolicy`** (`src/app/ui/memorypolicy.h`; a QML singleton, `main()` makes the app's) says
  what is kept for speed and when the app is away — the kept views' limits (`keptViews`,
  `hiddenViewMs`), `buildAhead` / `inUse`, `letGo(hidden)`: "Navigation" → Away. What follows it:
  `ViewKeeper` (the panes' hidden views), `AppShell.letGoPopups()`, `BlockEditor.wholePage` (a
  page created whole keeps only the rows near the view while the app is away), `SpellCheck`
  (`unload()`), the extensions' catalogs (`Engine::dropData()`), the on-device models (`LocalAi`,
  `SemanticSearch`: an idle one leaves memory at once).

For contributors:

* The frame's size never depends on `near` (rule 1 below: final height at creation). A picture
  with a size in its header has its frame from it. One without is loaded as soon as it exists,
  wherever it is (`NearView.enabled` only once it was measured: its row grows then, not under a
  reader who scrolls to it), and keeps the size it measured (`seenSize`, of that file: `seenOf`)
  after it let go; so does one whose loaded shape differs from its header's. A row never changes
  its height because a picture came or went.
* Anything that holds megabytes per item in a list — a decoded picture, a rendered page — asks
  `NearView.near` on the item that has its *place* (not on the `Image`: its size may follow what
  it loaded) and gives the decoder its shown size, not the file's.
* Tests: `tests/app/memory` (`tst_near_view`: items in, near and far from the view, the
  hysteresis, hidden items and windows, items that come and go mid-look, a QML page of pictures
  loaded and let go by frames alone; `tst_memory_keeper`: when memory is given back, the decode
  sides), `tests/platform/tst_processmemory` (what the allocator gives back, by the process's
  resident size), the shell's self-test ("5r": a page of thirty photos — the frames' sizes, which
  are loaded, at what size, and that the page is as tall after scrolling to its end), and the
  `pictures` scenario of tests/bench/app.

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
  the days on the way like "Go to day" does, not at every mouse move. While the bar is held
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

### Wheel and scroll bar: `SmoothScroll` (`src/app/ui/smoothscroll.h`)

The mouse wheel and the scroll bars move like a web browser's smooth scrolling (Firefox's
`ScrollAnimationBezierPhysics`, Chromium's wheel animations), everywhere: an application event
filter (`SmoothScroll::instance()`, installed by `main()`; library `brulion_ui_smoothscroll`, Qt
Quick's public API only) takes the wheel events Qt Quick delivers to an interactive Flickable.

* **Why.** Qt 6.11 makes each notch a short wheel flick (about 100 ms) whose velocity is averaged
  from the time between notches and capped (`QQuickFlickable::wheelEvent`): the distance depends on
  how fast the wheel turns — 6 notches 150 ms apart moved 420 px, 40 ms apart 284 px, 10 ms apart
  118 px instead of 6 × 72 (ctest `tst_smooth_scroll`, `qtWheelLosesNotches`, pins this down; it
  fails when Qt changes).
* **A notch** (120 eighths of a degree; a high-resolution wheel's parts in proportion) scrolls
  `QStyleHints::wheelScrollLines()` (the system's setting, 3) lines of 19 px: 57 px, as Firefox's 3
  lines of a 16 px font and Chromium's 53 px on Linux. The browser build takes the browser's own
  distance (the event's `pixelDelta`).
* **The motion** (`SmoothScrollAxis`, pure): an even deceleration to a stop at the target (an
  ease-out like Firefox's bezier(0, 0, 0.6, 1)), lasting twice the mean of the last three
  intervals between events within 50–200 ms (Firefox's rule: a single notch takes 200 ms, a fast
  wheel gets snappier). A notch while it runs adds to the target — no notch is lost or counted
  twice however fast the wheel turns — and never slows the motion down; a notch the other way
  turns back from where it was heading. Never past the content's ends (no overshoot).
* **Through Flickable's own `flick()`**, with the deceleration and the velocity of the plan
  (`flickDeceleration` and `maximumFlickVelocity` are set for the call and restored at once; the
  running flick keeps what it started with). So the view is `moving` and `flicking` as with Qt's
  wheel: the scroll bar shows, `movementStarted` / `movementEnded` reach the views that listen
  (the editor's `userMoved`, the sidebar, the PDF viewer), ListView fills its cache ahead; a press,
  a drag or a new position (`positionViewAtIndex`, the thumb dragged) stops it like any flick.
* **Left to Qt:** touchpad scrolling (scroll phases: the content follows the fingers 1:1, as in a
  browser; it also takes over from a running smooth scroll), Ctrl / Alt / Meta + wheel (the zooms
  of the PDF viewer, drawings and the graph handle Ctrl+wheel before the Flickable anyway), views
  that snap (`snapMode`, `StrictlyEnforceRange`), a view that is not `interactive`, and a view
  already at its end in that direction — the event goes on to the Flickable around it (a table
  inside a page, a list inside a panel), which scrolls smoothly in turn. Shift+wheel scrolls
  sideways where the view can (a browser's).
* **The scroll bar** (`BScrollBar`, so `BRowScrollBar` too; mouse UIs): the thumb dragged follows
  the pointer as before; a press on the track beside it pages towards the pointer — 7/8 of the
  view between its margins (Chromium's step), smoothly (`SmoothScroll.scrollBy`) — and, held,
  pages again after 250 ms and then every 50 ms until the thumb reaches the pointer; Shift+press
  keeps Qt's jump of the thumb to the pointer. (Qt's own track press jumped there on release.)

`BRULION_SMOOTHSCROLL=0` turns it off (Qt's wheel flicks, the bar's page as a jump; A/B
comparisons). A type check per event, a few property reads per wheel event. ctest
`tst_smooth_scroll`: the plan (data-driven: one notch from rest, notches 150–4 ms apart adding up
exactly and never slowing down, durations following the rate, the ends, turning back), and lists
in an offscreen window turned by wheel events — one notch (also a high-resolution eighth) eased
into without overshoot, 6 notches at any rate moving exactly 6 × 57 px, the ends, a nested list
passing the wheel on at its end, Ctrl / Alt / touchpad / a snapping list left to Qt, Shift and a
tilting wheel sideways, `scrollBy` pages adding up, a new position stopping it.

### Measured (before → after this work)

Scroll probe, phone 412×915 touch and desktop 1280×800 touchpad; "big" = 120–365 generated
journal days (`make-vault.py`); before = v0.1.1 with the probe added.

* x86 laptop (Xvfb, llvmpipe, RelWithDebInfo, two runs each): big journal, desktop touchpad —
  longest frame 2.9 / 2.7 s → 82 / 60 ms, p99 1025 / 882 → 16 ms, layout shifts 9 / 10 frames
  (2933 / 4640 px, single jumps up to 1566 px) → none; phone flick — frames over 100 ms 10 → 0,
  longest 144 → 63 ms; guide page "Welcome to Brulion", desktop touchpad — longest 574 / 311 → 82 /
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
  "Show pictures from the internet without asking" on, its frame (320 × 180, or its placeholder's height)
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
(*Properties*) and, for tags and aliases, in the page header; nobody has to touch the raw text
(it stays one menu item away: "Edit as text"). Each change is one undo step of the page
(Ctrl+Z, a toast's "Undo") and rewrites only the lines of that property — comments, order,
quoting and line endings of the rest stay as they were (`markdown/pageprops.h`, every YAML edit
verified by parsing the result). The page's properties are the YAML block and the first block of
`key::` lines, as the index sees them: more `key::` blocks after a blank line are shown as property
rows but are the page's text, not in the panel. An edit reads the whole text again: a lone fence
being typed ("```js" in the edited block) is closed first, as the save writes it (also before a
note mode switch or an inserted template), so it never runs on over the page.

**The panel** (`qml/editor/FrontmatterView.qml` → `PropertiesPanel.qml` → `PropertyRow.qml`,
`PropertyListValue.qml`, `PropertyMenu.qml`, all for the leading frontmatter row's delegate; a
chip's menu is `PropertyChipMenu.qml`, the page header's chips have the same one):

* Folded (the default: the journal stream stays dense): "Properties", the count and a one-line
  preview of the values ("status: in progress · cost: 1200 · due: Thu, Oct 29" — a date or a date
  and time as the open panel's date button shows it, `DatesHelper.formatShort`). On a page, whose
  header shows the tags and aliases as chips while the panel is folded
  (`BlockEditor.headerShowsListChips`, set by `PageView`), the preview leaves those properties out:
  the tags are in the header while the panel is folded and in the panel while it is open, never
  both (the count still counts them). A journal day in the stream has no such header and keeps
  them in its preview. The header's tap switches it; whether
  it is open is a device setting (`AppSettings.propertiesExpanded`, key
  `editor/propertiesExpanded`; `BlockEditor.propertiesExpanded` bound by `PageView` and the
  journal days, `propertiesExpandedToggled` writes it), so a page stays open while it is edited,
  undone or synced. Expanded, the header has "+" (add, Ctrl+;) and "⋯" ("Edit as text": the
  raw text in the block's inline editor, "Collapse").
* A row: the type icon (Text `type`, Number `sigma`, Checkbox `square-check-big`, Date `calendar`,
  Date and time `calendar-clock`, List `list`, Tags `tags`, Aliases `bookmark`, other `code`), the
  key and the value's editor by type. Text is shown rendered (`[[links]]`, `#tags`, URLs work —
  the `properties` role's `html`, escaped by the `InlineRenderer`); a click beside a link opens a
  `BSuggestField` with the raw text (the notebook's values of the key as suggestions, after `[[`
  pages); a text of several lines gets a text area (Ctrl+Enter or leaving it commits). Numbers
  take "3,5" and "1 200" (an invalid one: the field's error, nothing written). A checkbox writes
  each toggle. Dates open the editor's date dialog (`EditorDialogs.pickPropertyDate`: the key as
  title, natural language "tomorrow" or "jutro", the clock for a date and time, "Clear"). Lists, tags and
  aliases are chips (a `[[Page]]` item and a tag open their page; × removes — on touch UIs the
  chip's menu, as the page header's chips: a press and hold on any chip, a tap on one that opens
  nothing, with its page or tag page, "Copy" and "Remove" and "Undo" in the toast; the chip has
  no × there, its 44-px target made each chip 44 px wider; Delete on a focused chip everywhere) with "+": a field
  where Enter or "," adds (a pasted "a, b, c" adds three) and stays open, Backspace in the empty
  field takes the last chip, Ctrl+Z there undoes; what cannot be added (an alias with `|`, a tag
  that is none) is said under the field, and in a toast when the field is left. Fields commit on
  Enter and when they lose the focus (also when the row goes away, and before the editor shows
  another page: `PageController.pathAboutToChange`), Escape leaves them unchanged. A field starts
  with the value of the moment it opened — not a binding: the notebook changing (sync, another
  pane, the page's own save) re-renders the rows and must not replace what is typed — and writes
  only what differs from it, so a value opened and left as it was keeps its form (a `|` text,
  `ilosc:: 1.50`).
* The row's menu (its type icon, a right-click or a long press on the row — not on a chip, which
  has its own menu —, the Menu key /
  Shift+F10): "Type" (Text, Number, Checkbox, Date, Date and time, List; tags and aliases keep
  theirs — a value that cannot be converted is cleared with "Undo" in the toast; an empty value's
  type is remembered for the session, `BlockEditor.propertyTypeOverride`, a list is written `[]`),
  "Rename" (the key becomes a field with key suggestions; an existing name, `brulion` and a
  key a `key::` line cannot hold are refused with a sentence under the field), "Copy value",
  "Delete" ("Undo" in the toast); a row the fields cannot edit (a map, a nested list, a block
  Brulion cannot edit safely) offers "Edit as text" instead.
* "+ Add property": a field with the notebook's keys (key, its type's icon, "12 pages";
  `PageController.suggestPropertyKeys`, without the page's own keys and `brulion`). Choosing or
  entering a key writes it at once with an empty value of its type in the notebook (`key:`,
  `key: []`, `key: false`; `PageController.addPageProperty`) and puts the keyboard on its value;
  a key the page has already (in either block, in any case) is refused under the field — adding
  never overwrites a value; Escape drops it, nothing written.
  The type of an empty value is the key's most common type in the notebook
  (`PageController.notebookPropertyType`, from `VaultIndex::propertyKeys()`).
* A block Brulion cannot edit safely (an indented mapping, plain text between `---` lines) shows
  its values read-only: "These properties cannot be changed safely in fields." with
  "Edit as text".
* Layout: the type icon and the key in a column (as wide as the widest key, at least 96 px; keys
  and values at the UI text size, so a value opened for editing keeps its size; list
  items are filled chips, a page link marked by its page icon; on touch UIs a chip is at least
  44 × 44, and the list's "+" never starts a line alone — the last chip goes down with it,
  `PropertyListValue.placement`) beside the value — on phones too, rows 44 px on touch UIs (32 px,
  28 dense, with a mouse), list rows too: a value's line is centred in the row's height
  (`PropertyRow.valuePadding`; a list's chip line is its tallest item, `PropertyListValue.lineHeight`
  — the chips' 44-px targets on touch UIs, the 28-px "+" with a mouse), so a list of two lines is
  92 px on touch UIs, one line always the row's height. The key goes over the value (every row of
  the panel) only where the two columns would cut a key or squeeze the value: the widest key does
  not fit 40 % of the panel ("ostatnia aktualizacja" on a phone; a touch screen has no tooltip to
  read a cut key), or the value column would be narrower than ~168 px (a narrow side panel) —
  `PropertiesPanel.stacked`; a key is never elided beside its value. Every control and tap area is a 44 px target on touch
  UIs: the key's whole cell (a tap focuses the row, a double tap renames), the value's whole cell
  (a text, a number, a date button: the row's padding included). The open panel is a sunken surface
  in light and a raised one in dark (`bgElevated`: lighter reads as raised there, the sunken fill
  barely parted from the page), without a frame (the focus border while its text is edited), 24 px above the first block under
  it (folded or open, in both modes, also under a page title drawn above the panel;
  `BlockDelegate.belowProperties`). Heights are bindings (rule 1 of
  "Scrolling performance": the rows' heights summed, the chips' lines computed), rows live in a
  `ListModel` by key so an edit keeps them (a focused field too).
* Keyboard: rows take the focus (Tab); ↓ / ↑ move between them (past the last: the add row), → or
  Enter edits the value, ← or F2 renames, Delete or Ctrl+Backspace removes, Alt+↓ goes to the first
  block, Ctrl+; adds, Ctrl+Z / Ctrl+Shift+Z undo and redo, Escape returns to the editor. A field
  with the keyboard ends block editing and is kept above the on-screen keyboard
  (`BlockEditor.editingProperty` in `ensureEditVisible()`); the touch toolbar stays away.

**Adding the first property** (`BlockEditor.addProperty()`: the page menu's "Add property",
Ctrl+;, the palette's "Add page property" — uiAction `addProperty`, handled by `PageView` and
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
"Open tag page", "Remove from this page"; an alias (also a tap): "Copy", "Remove alias"
(`removePageListItem`: from every property holding it; the last one takes the property away;
"Undo" in the toast). The row ends with "+" (when properties can be added): "Add tag", "Add
alias" (a field at the end of the row — tags suggested —, Enter adds and stays open, Escape
closes; leaving it adds what it holds, a refusal said in a toast) and "Add property…". The
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

## Page icon and cover

A page can have an **icon** — an emoji or one of the app's icons — and a **cover** picture, both
page properties (docs/SYNTAX.md §4 "Page icon and cover": `icon`, `banner`, `banner_y`, the keys
Obsidian's Iconize and Banners plugins and Logseq read), so they go with the file to other devices
and apps. `PageController.look` ({ icon, cover }) follows the page's text as it is edited (in source
mode its own edits, and what was typed there once the source is left); the lists follow the index (`Workspace.pageIconOf(path)`, `Workspace.propertiesRevision`: not every
save, only a change of some note's properties).

* **Setting them**: the page menu ⋯ — "Add icon…" / "Change icon…", "Add cover…" / "Change
  cover…" —, a click on the icon in the header, the cover's "Change cover". Each change is one
  undo step with "Undo" in its toast (`PageController.setPageIcon`, `setPageCover`,
  `setPageCoverPosition`: single-property edits, the rest of the file byte for byte).
* **The icon dialog** (`qml/content/ContentPageIconDialog.qml`): a field to type or paste any emoji
  (a phone's emoji keyboard too; Enter or "Use"), a grid of common emoji (all drawn by the app's
  emoji font where the system has none — the browser), a grid of the app's icons (Lucide, written
  `LiBookOpen`), "Remove icon".
* **The cover dialog** (`qml/content/ContentPageCoverDialog.qml`): the notebook's pictures as
  thumbnails with a search by name (`Workspace.pictureAttachments`), "From file…" (stored in the
  attachments folder like an inserted picture: `Workspace.importAttachment`), an address of a
  picture from the internet ("Use address"), "Remove cover".
* **In the header** (`ContentPageHeader`): the icon before the title, at the title's size (a page
  starting with its own "# Title" has it on a row of its own above the chips); the cover as a band
  above everything (180 px, 112 on phones and in the side panel), the picture filling its width,
  `banner_y` choosing the part shown. Its buttons (on hover with a mouse, always on touch UIs):
  "Reposition" — then a drag moves the picture up or down, "Save position" writes `banner_y`,
  "Cancel" — and "Change cover". A cover from the internet goes the web-picture way (docs/SYNTAX.md
  §2): "Cover from <site>" and "Show" until the user allows it (or the device setting does), a
  spinner, the reason it failed with "Try again"; QML never loads it from the network. A file
  that is not in the notebook is named ("No picture “…” in the notebook").
* **In lists**: the sidebar's tree (in place of the file icon), favourites (of the star), recent
  pages (of the clock), All pages, the quick switcher (`Workspace.switcherPages`: `icon` / `emoji`),
  as `BPageIcon` / `BListItem.iconEmoji`.
* **In links**: rendered text puts the icon in front of a wiki link or a Markdown link to the page
  (`RenderContext::pageIcon`, `EditorHost::pageIcon`): an emoji as text; an app icon as three no-break spaces
  in an anchor named `brulion-icon:<name>`, which `RichTextPills` reports as a pill of kind "icon"
  for the text's Repeater to draw a `BIcon` in the link's colour. Exports leave links as they were.
* **Tests**: `tst_markdown_pagelook` (reading, writing, property round trips byte for byte, link
  icons), `tst_editor_look` (`PageController.look`, the edits as undo steps), `tst_richtextpills`
  (the icon pill).

## Note options

A note's own options (docs/SYNTAX.md §4 "Note options") — page properties, so they travel with the
file — win over the device's settings for that note: `PageController.noteOptions` ({ lineNumbers,
pictureFrames, pictureBacking: "" | "on" | "off", textWidth: "" | "narrow" | "normal" | "wide" |
"full", tasks: "" | "ignore" }; "" — the device's setting) follows the page's text as it is edited
(source mode too), typed for the views as `codeLineNumbers`, `pictureFrames`, `pictureBacking` (-1
the device's, 0 off, 1 on) and
`textWidth` (-1, or the pixels Settings stores: 600, 680, 960, 0 for the full width).

* **Where they show**: `CodeBlockView` numbers the lines with the note's `codeLineNumbers`, else
  `AppSettings.codeLineNumbers`; `EmbedView` frames pictures with the note's `pictureFrames`, else
  `AppSettings.pictureFrames`, and in a dark theme lays them on a light backing with the note's
  `pictureBacking`, else `AppSettings.lightPictureBacking` (so does the note's `WebPictureViewer`;
  the picture view of an attachment, shared by every note, follows the device's setting);
  `PageView` keeps the text within the note's `textWidth`, else
  `AppSettings.editorWidth` (a page's own view and a daily note opened as a page; the Journal's list
  of days keeps one width for all of them). `tasks` is the property of SYNTAX §5 "Ignoring a page's
  tasks" (`Workspace.tasksIgnored`).
* **The dialog** (`qml/content/ContentNoteOptionsDialog.qml`): the page menu ⋯ and the top bar's More
  → "Note options…" (`requestUiAction("noteOptions")`; pages, not in reading mode or a read-only
  view). A switch "Tasks in Tasks and the calendar", then "Line numbers in code blocks", "Frames
  around pictures", "Light background under pictures" (in a dark theme) — As in Settings | On |
  Off — and "Text width" — As in Settings | Narrow |
  Normal | Wide | Full —, each row saying what the device's setting is. A choice is written at once
  (`PageController.setNoteOption`: one undo step, only the option's line changes; a refusal — a
  frontmatter Brulion cannot edit safely — in a toast); the properties panel shows the keys as
  ordinary properties.
* **Tests**: `tst_markdown_noteoptions` (values, keys, which key decides, written values read back),
  `tst_editor_noteoptions` (`PageController.noteOptions`, `setNoteOption` with its keys, languages,
  undo steps, removal and refusals).

## Note modes: outline and document

Every page and daily note has a mode — docs/SYNTAX.md §4 "Note mode": the frontmatter key
`brulion: document | outline`, else the notebook's journal / page default and a content
heuristic (`markdown::resolveNoteMode`). Both modes edit the same `BlockDocument` with the same
inline editor; only the layout and a few keys differ. It is switched by the page header's toggle
on a wide header (its icon shows the mode), the page menu "⋯" ("Switch to outline / document"
— on phones and in the side panel the only place in the header), the top bar's More and the
palette ("Toggle note mode").

* **Outline** (like Logseq) — the block editor above: a bullet for every block, guide
  lines, folding, zoom into a bullet, Tab indents.
* **Document** (like Obsidian's live preview) — the note reads as one continuous
  formatted document: no bullets or guide lines for paragraphs and headings; headings large with
  document spacing (`BlockEditor.headingGapAbove`: 1.8 / 1.5 / 1.25 / 1.05 em above an H1 / H2 /
  H3 / smaller heading, `headingGapBelow` 0.6 em under every one — clearly closer to what follows,
  with air on both sides); paragraphs as normal text; lists as ordinary Markdown lists inside the flow
  (bullets, numbers, checkboxes, indented per nesting level, with a thin guide line per nesting
  level — `Theme.guide`, as in the outline — at each ancestor level's marker centre, under a
  number's column, and from a parent's marker down to its children; never through an item's own
  marker); a comfortable maximum text width.
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
tools — a list button (paragraph ↔ list item), Task, Link to page, Tag, Commands — and
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
visible. The "Properties" panel never lists the key.

Choosing and switching the mode:

* **New page dialog** (`ShellNewPageDialog`, Ctrl+N, and `ContentNewPageDialog` in "All
  pages", both with `ContentNoteModeChoice`): "Document (like Obsidian)" / "Outline (like
  Logseq)", preselected from the last choice on this device (as long as the notebook's
  `pageMode` is still the one it was made under), else from `pageMode`. The key is written only when the choice differs from
  what the resolution gives for the initial content (`markdown::withNoteModeChoice`). Pages
  created from unresolved `[[links]]` and by the quick switcher's "create" take the default
  without asking.
* **Switching**: a compact toggle in the page header next to the star (its icon shows the current
  mode; tooltip "Document mode" / "Outline mode"), the page menu item "Switch to document" /
  "Switch to outline" and the command palette's "Toggle note mode" (in the journal stream:
  the day being written in). A switch always writes the key (`markdown::setNoteModeKey`) and
  changes nothing else in the file; frontmatter that cannot be edited safely is left alone and a
  toast says so.
* **Settings → Notebook**: "Default mode of new pages" and "Journal mode" (both: Document |
  Outline; defaults document and outline) — `pageMode` / `journalMode` in
  `.brulion/config.json`, synced.
* **Settings → Notebook, "Settings from Obsidian"** (only while the notebook has no
  `.brulion/config.json` and settings were taken from `.obsidian/`, `Vault::configAdoptedFromObsidian`
  → `Workspace.configFromObsidian()`): the row above "Journal folder" lists the settings taken
  (their row titles: Journal folder, Journal file names, Journal template, Templates folder,
  Attachments folder) and says that changing any setting saves them to `.brulion/config.json`;
  **Save to notebook** (`Workspace.saveAdoptedConfig()`) writes them at once (for the browser
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
  `NextcloudSetup`, in the sync center and Settings → Sync): the server, the user and
  the app password are shared, the switch and the server folder belong to the open notebook (no
  notebook open: nothing syncs). A notebook without a folder of its own is offered "Notatki" (Notes)
  for the first account on the device, else its own name ("Praca", "Praca 2" when taken —
  never a folder another notebook has, holds or is inside); one synced before keeps the folder
  its sync bookkeeping names. A folder another notebook on the device syncs with (its folder
  exists, its switch is on) is an error under the field at once ("The notebook “Dom” already
  syncs with this folder…") and is not saved while sync is on. Two notebooks that 0.3.1–0.3.5
  left on one folder (or one inside the other) sync nothing while that lasts — no automatic
  sync and no "Sync now" (it only says why: one click would mix the two notebooks for
  good, and choosing another folder is one step away): the status ("Not syncing: the notebook
  “Dom” on this device syncs with the same server folder…") stays in the sync
  footer and the sync center (red, error code `folder-shared`), the event log says it once per
  notebook opened and folder, a toast too, and the overview's Nextcloud card offers "Choose
  another folder" (the Nextcloud tab, whose form shows the error under the folder). "Sign out"
  switches sync off in every notebook (the dialog says so), saving another server or user in all
  the others. An app password whose owner-only file cannot be written is an error toast ("The app
  password could not be saved.") and "Save" changes nothing, the typed password staying for
  another try; one "Sign out" cannot delete is a toast too. The switch and folder of 0.3.0 and
  older go to the notebook they belong to (AppSettings).
* **A folder of the phone** (`qml/io/DeviceFolderPanel.qml`, Android, a notebook in the app's
  storage: the sync center's Syncthing tab, and the row "Folder on this phone" of Settings → Sync;
  docs/ANDROID.md "A folder picked in the system's dialog"): not linked — why and **Choose a
  folder…** (the system's folder dialog, `AppController.linkDeviceFolder()`); linked — "Folder
  “Notatki” on this phone", where it is ("primary:Syncthing/Notatki"), the sync's state with a badge
  (On / Syncing / Problem), **Sync now**, **Choose another folder** and **Stop syncing**, which says
  in place what it does (the notes stay in both) before it unlinks (`unlinkDeviceFolder()`). With a
  folder the sync is the folder's (`Workspace.sync.deviceFolder`): the Syncthing tab shows the
  status card, the deletion guard and the history below the panel, in the folder's words ("Checking
  files in the folder “Notatki”…", "Brulion no longer has access to the folder “Notatki”. Choose it
  again in Settings → Sync.", "deleted … in the folder", "Operations in the folder" in a run's
  details); the Nextcloud tab says that Nextcloud is off for this notebook (one way per notebook)
  and hides its form; the overview's Syncthing card carries the folder's state. On a computer,
  `BRULION_TEST_PLAY=1` in the environment shows this interface and the start screen of the app for
  Google Play (`AppController.deviceFoldersAvailable`, `playStore`) with local folders — to look at
  it and drive it with the remote control without a phone; nothing else changes.
* **Live progress** (`qml/io/SyncLiveProgress.qml` in the Nextcloud tab's status card, from
  `Workspace.sync.live`, at most four updates a second and once a second for the elapsed time;
  no timer while idle): the phase in words ("Checking files on the server…", "Sending and
  downloading changes"), "folder 12 of about 40" / "120 of 3125 files checked · changes: 3", a bar
  (indeterminate until the files are known), "Running for 1 min 20 s · 1.4 MB · after an edit ·
  #42", the files in flight ("Now: …") and "Stop". When nothing arrives for 20 s an amber
  box says "The server has not answered for 25 s…" with what Brulion waits for; after 180 s the run is
  stopped and retried — the panel never says "syncing" forever. `Workspace.sync.statusText`
  (sidebar footer, settings) follows the phases at the same pace.
* **Conflict copies of drawings** (`SyncConflictResolver.qml` over `ConflictResolver`, list
  entries `drawing`: the copy or the original is a drawing — an SVG with an Excalidraw scene,
  `*.excalidraw.md`, `.excalidraw` —, a pen icon in the list): never the text merge (a line merge
  would cut the scene's payload). A card "Two versions of a drawing" with read-only previews
  (`DrawingCanvas { interactive: false }`: the whole drawing in view, centred both ways, no
  input, the app's theme;
  a drawing Brulion does not draw as its picture, `Workspace.drawingUrl`) of this device's
  version, the copy and — when both are drawings Brulion edits, of one format — "After merging",
  the elements of each ("3 elements") and "2 in both, 1 only here, 1 only in the copy"; on
  phones two previews in a row and the merge below, in a scrolling area. **Merge drawings**
  writes the union (core `drawing::merge` with no common version: Syncthing keeps none — the
  original's elements, order, paper and Markdown around the scene, plus the copy's other
  elements on top; for an element both have the higher version, a tie the original's) into the
  drawing (`WorkspaceSync.resolveConflictBytes`: its previous version in History, unthrottled)
  and moves the copy to the trash. Elements erased on one device only come back (the card says
  so). Same elements (another program wrote the picture) → "The copy has the same drawing"; the copy
  contained in the drawing → "Everything in the copy is already in the drawing" ("Remove the copy"). A drawing
  Brulion cannot merge (a frame, a picture without a scene, a broken one) gets only "Keep
  mine" / "Keep the copy" (the copy's exact bytes, the replaced version in History). An
  Obsidian plugin file (`*.excalidraw.md`) is a note too: its Markdown around the scene
  (frontmatter, the back of the card, element links, embedded files, text entries) counts
  (`drawing::keepsMarkdownOf`: the copy's must survive in the original's — text entries follow
  their elements). When the copy's notes would be lost, nothing is "the same" or "older": "Merge
  drawings" (the card says so) merges the elements into the drawing and **keeps the copy**
  (`ConflictResolver.updated`, a toast, the resolver stays open); then — or when the copy adds
  nothing to the drawing at all, a merge would leave it as it is (the copy's own paper, an element
  edited by hand: never taken) — a card "Only the notes around the drawing differ" with a
  `BDiffView` of both files' Markdown (the scene's block left empty, `oursNotes`/`theirsNotes`)
  and "Remove the copy" once the user carried over what they need in Obsidian. The replaced version
  goes to History whatever its size (`writeBytesKeepingHistory` records it from the disk: the
  vault snapshots only the content it holds in memory, up to 16 MB; nothing is written when it
  cannot be recorded). A decision made on versions that changed meanwhile is refused and shown
  again. Desktop: "Open the drawing" in the header. Tests: `tst_appio_conflicts`;
  `tests/app/io/syncthing-conflict.sh` (no ctest, needs `syncthing`: two Syncthing instances on
  127.0.0.1 change an SVG and an Obsidian drawing while paused, their own conflict copies merged by
  `realSyncthingConflicts` on both devices' folders). Nextcloud's own sync merges a drawing changed on both devices by itself, against
  the version both had last (`drawing::mergeDrawingFiles`); only drawings it cannot merge so
  become such copies.
* **Retry and decisions:** a failed run shows "Next attempt in 25 s" (a countdown) with
  "Try now"; held-back deletions and conflicts are "Waiting for your decision" / "Conflict in
  … — merge the versions" (the deletion guard and the conflict list stay as before). A notebook
  folder that lost every synced file ("local-empty": nothing was deleted on the server) offers
  "Download the notes from the server" ("Copy the notes from the folder" for a device folder;
  `WorkspaceSync.downloadFromServer`): one run that forgets the bookkeeping first
  (`SyncRunOptions::startOver`), so the first sync's rules bring the files back and delete
  nothing. Moving or renaming
  folders and pages never asks: content that arrives under a new path in the same run is a move,
  not a deletion (`SyncEngine`, docs/ARCHITECTURE.md "Sync guards").
* **Folders** (Nextcloud): empty folders travel both ways, an emptied folder deleted on one side
  goes on the other, a folder or page renamed in the app is moved on the server (one MOVE), and
  one renamed on the server is renamed here too. The status card, the history row ("1
  folder moved", "2 files moved", "2 new folders") and the run's details
  ("Folders: 2 new, 1 deleted, 1 moved on the server, 1 moved here";
  `WorkspaceSync.folderCountsText`, and "Files: 3 moved on the server, 1 moved
  here"; `WorkspaceSync.fileMovesText`) name them; folders a run created or removed here reach the
  tree at once (`WorkspaceSync.foldersChanged` -> `Workspace.notifyFolderChange`: folders make no
  Vault change of their own), a folder the run renamed here as the server had renamed it follows
  in the tree and the routes (`WorkspaceSync.folderMovedHere` -> `Workspace.folderRenamed`), and
  a folder the user creates, deletes or renames starts a sync after the debounce like an edit
  (`WorkspaceSync.notifyLocalChange`).
* **History** (`SyncHistoryList.qml`, card "Sync history"): the last runs (20 kept,
  5 shown, "Show all"), quiet ones folded ("No changes ×3, 09:25–09:55"); each row: the
  result in words with a coloured icon (never colour alone), when, why ("by hand", "after an
  edit", "retry", …), what moved and how long. "Last run: … · Details" under the
  status. A row opens **`SyncRunDetails.qml`** (a dialog; bottom sheet on phones): the result
  with its explanation, code and technical message, "Where the time went" (a bar of the phases
  plus a list for screen readers), the changes, the requests to the server (count, average,
  bytes, protocol, timeouts, the slowest), the files that failed with a Polish explanation per
  code, conflicts ("Merge"), "Show in the event log" (the log at the run's time) and "Copy
  details". The card's "⋯": Event log (its sync entries) and, after a separator, "Clear
  history…", asked first ("Clear the sync history?": the runs and their details go, the notes
  and the event log stay; it cannot be undone).
* **Event log** (`qml/diag/LogsView.qml`, route `logs`; from Settings → Application (with a
  warning count) and Sync, the sync center, the palette — also found by "event", "logs",
  "diagnostics"): a search field (case and diacritics ignored), area chips (All, Sync, Network, Notebook, Index, Reminders, Import and export,
  Application, System), the level (All / Warnings and errors / Errors), chips for a run's time window and
  for the detailed mode; the list (`LogRow`: time, level icon + letter, area, the message as
  plain text, "×37" for repeats; warnings and errors tinted; day headers; earlier runs of the
  app included, "Hide earlier runs of the app") follows new entries at the end, otherwise a
  "New entries (n)" button appears; long press / right click copies a line. Actions: Copy
  (what is shown, with the header; ≤ 256 KB), Share (Android share sheet), Save to
  Downloads (Android), Save as… (desktop, Android SAF), Download (browser), Open folder
  (desktop), Detailed log, Clear… (confirmed). The footer: the detailed switch and what
  the log contains (no passwords or tokens; note names and the server may appear; it never
  leaves the device unless shared). Phones: the meta data above each message, Copy in the
  menu.
* An export starts with a translated header: version, device, Qt, time and detail level, what
  it contains and what it never contains, and the last five sync runs.

## Notebooks from GitHub

Markdown notes kept in a GitHub repository — the whole repository or one folder of a branch — are
a notebook (`Workspace.github`, `shell/workspacegithub.h`, over the core module `github`:
docs/ARCHITECTURE.md). One implementation everywhere, the browser included: Brulion talks to
GitHub's REST API (api.github.com answers the browser's CORS requests), not to Git — no git
program or library, and only the folder's files travel, never the history. Other Git hosts are
not supported (GitLab, Codeberg, a server of one's own: a Git client beside a Syncthing or plain
folder does that on the desktop).

* **Opening one** — the start screen's card "Notes on GitHub?" → "Open from GitHub…"
  (`qml/io/GitHubOpenDialog.qml`), also "Open as a new notebook…" in the sync center's GitHub tab:
  the address as pasted — `owner/name`, a github.com page of the repository, of a folder
  (`…/tree/<branch>/<folder>`) or of a file (its folder), a git remote (`git@github.com:…`,
  `ssh://`), a raw.githubusercontent.com file — with a line saying what Brulion understood; the
  branch (empty: the address's — GitHub is asked which leading segments name a branch, as a branch
  name may hold "/" — else the repository's default) and the folder (empty: the address's, else the
  whole repository) as fields; a token when none is saved (optional: public repositories open
  without one); on the desktop the folder on this device, by default the app's storage:
  `<app data>/GitHub/<owner>/<name>` (with " – <folder>" for a folder; " 2", " 3"… when another
  link or other notes have that folder). GitHub is asked first (the repository, the branch; an
  empty repository is fine — the first commit fills it); then the notebook opens on the sync
  center's GitHub tab and its notes arrive. In the browser the notebook lives in the app data
  folder kept in IndexedDB (docs/WEB.md), like the documentation, and is one of the recent
  notebooks of the switcher.
* **Linking an open notebook** — the sync center's tab **GitHub** (`qml/io/GitHubPanel.qml`,
  between Nextcloud and Conflicts; the overview has a GitHub card): "Keep this notebook on GitHub"
  with the same address, branch and folder → "Link this notebook" (checked with GitHub; the
  link is the notebook's on this device: AppSettings `github/notebooks/<notebook id>/…`, never in the
  notebook). The first pull of a notebook with notes of its own keeps them: files only here stay
  (changes to commit), files that differ get GitHub's version as a conflict copy. In the
  documentation notebook the tab instead says how to correct it: "Open the documentation from
  GitHub…" opens the guide's folder of Brulion's repository
  (`janusz-bit/brulion`, `src/app/shell/guide/<language>`) as a notebook of its own, whose
  corrections are proposed as a pull request (the documentation notebook itself is written
  again by every version).
* **Getting the newest version** ("Get the newest version", the sync footer's ⟳ while Nextcloud
  is off; automatically 1.5 s after the notebook opens, every 15 minutes while the app is in
  front and when it comes back after 5 minutes): an unmoved branch costs one request
  (If-None-Match: GitHub's 304 is free of the rate limit), an unchanged folder no listing; files new
  or changed on GitHub and unchanged here are downloaded (verified against the blob id the tree
  names: a proxy's error page is never written), files removed there go to the trash (their
  emptied folders go: Git keeps none), a file renamed or moved there (its content under one new
  name) is renamed here too — its history, favourite and folds follow; changed on both sides — text merged three-way like
  Nextcloud's (line by line, the config key by key, drawings element by element), else this
  device's version stays and GitHub's is saved beside it as `Name.conflict-GitHub-<stamp>.md`
  for the Conflicts tab (once, however often it pulls); removed here and changed there:
  restored; changed here and removed there: kept (committed again). Left out and listed under
  "n file(s) left out or not done": files over 50 MB, Git LFS files (their pointers), symbolic
  links, submodules, names that clash in letter case; hidden files and folders (`.gitignore`,
  `.github/`) are the repository's and never touched, `.brulion/config.json` travels. A pull
  that failed for some files does not count as complete: the next one compares everything
  again. A branch or folder that lost every file the notebook had removes nothing here ("The
  folder on GitHub has no files any more…"). Without a token the files come from
  raw.githubusercontent.com (by the commit), the API only for the branch and the trees — 60 API
  requests an hour are enough for that.
* **The notes not on GitHub yet** — "Not on GitHub yet" (new, changed, removed; "In the
  proposal"): every notebook file whose Git blob id differs from the version last pulled or
  committed (hashed again only when its size or time changed); a row opens the page. Conflict
  copies made here are never sent. The status: "3 change(s) not on GitHub yet · checked today 10:32",
  "The same as on GitHub · …", what failed, "Offline — Brulion checks GitHub again later" (no
  error: it tries again); the same line in the sync footer with the branch icon when Nextcloud
  is off (`WorkspaceGitHub.appStatus`).
* **Commit…** (only when the token may push there; `qml/io/GitHubCommitDialog.qml`): the message
  ("Update 3 note(s) from Brulion" proposed), the notes ticked (unticked ones stay here for
  later) → one commit on the branch, made on its newest version (pulled first): text inline in
  the new tree, other files as blobs, the executable bit and the repository's spelling of names
  kept; the branch moved only as a fast-forward — when someone pushed meanwhile, their version is
  pulled and merged and the commit made again (once). Refused, with the reason as a toast: no
  token, a token that may not push ("propose the changes instead"), notes whose GitHub version
  waits in a conflict copy ("Merge the conflicting notes first"), a pull that could not bring
  every file up to date, every pulled file gone here (a lost storage). An empty repository gets
  its first file through the contents API, the rest in a commit after it.
* **Propose changes…** — a pull request to the notebook's branch, titled and described in the same
  dialog: from a new branch `brulion/<yyyyMMdd-HHmmss>` of the repository when the token may push,
  else of the user's fork (made when missing — GitHub returns the existing one —, waited for until
  it has its branches); the card "Proposal #12" shows its state (Open, Merged, Closed) with "Open on
  GitHub", "Check its state" and, once it ended, "Forget". While it is open, proposing again
  ("Update the proposal…") adds one commit to its branch with every note it changes at this
  device's version (a merge commit when the notebook's branch moved meanwhile) — never a second
  pull request; a branch someone else changed on GitHub is never overwritten. The notes stay
  "changes" until the pull request is merged and pulled.
* **The token** (the card "GitHub account" on the tab; also in the opening dialog): a personal
  access token pasted, checked with GitHub ("Signed in as @jan"; a classic token without the `repo`
  or `public_repo` scope is told it can only read), kept in an owner-only file next to the
  Nextcloud app password (`<app data>/secrets/github-token`) and sent only to api.github.com
  (masked in every log line); "Make a token on GitHub" opens the classic token page with the
  `repo` scope; "Forget the token". A classic token is what proposing to other people's
  repositories needs (a fine-grained one only reaches its owner's repositories). No OAuth sign-in:
  GitHub's sign-in endpoints refuse a browser page, and Brulion has no client registration to
  hand out.
* **Unlink…** (confirmed): the notes stay, the bookkeeping goes (`RepoSync::resetState`);
  linking again starts as a first pull.
* Errors in words (`WorkspaceGitHub::errorText`): a token not accepted, one that may not do this
  (which scopes), the rate limit used up until a time ("With a token the limit is much higher"),
  not found ("A private repository needs a token"), the branch moved meanwhile, GitHub's own
  refusal quoted, offline, damaged on the way. Tests: `tst_github_address`, `tst_github_sync`
  (core, against the in-memory `tests/core/github/fakegithub.h`), `tst_shell_workspace_github`.

## Calendars (ICS subscriptions and CalDAV)

Calendars kept by other programs, per notebook and per device (`Workspace.calendars`,
`shell/workspacecalendars.h`; core `caldav`, docs/ARCHITECTURE.md). Nothing about them is written
into the notebook except synced tasks: the settings, the caches and the sync state live in the
notebook's device state (`<device state>/calendars/`), the passwords in owner-only files next to the
Nextcloud app password (`<app data>/secrets/caldav-<account>`). A second device sets up its own.

* **Subscriptions** — a calendar by its address (`https://…`, `http://…`, `webcal://` and
  `webcals://` become https): read-only, its events shown in the calendar view. Fetched when the
  notebook opens and then every 1, 6 (default) or 24 hours, and by hand; conditional requests
  (`If-None-Match` / `If-Modified-Since`: an unchanged feed answers 304), redirects followed to other
  servers but never from https to http, no cookies, at most 16 MB, an HTML page (a login, an error)
  refused as "no calendar". The copy last fetched stays when a fetch fails (the row says why). Its
  name and colour come from the feed (`X-WR-CALNAME`, `COLOR`, `X-APPLE-CALENDAR-COLOR`) unless
  chosen. Google Calendar's "secret address in iCal format" is such a subscription.
* **CalDAV accounts** — "Add a CalDAV account…" (`qml/io/CalendarAccountDialog.qml`): a preset
  (Nextcloud; Fastmail and iCloud fill in their servers; Google: its CalDAV needs an OAuth sign-in
  Brulion does not have — the dialog says so and suggests the secret iCal address), the server, the
  user and an app password; "Find calendars" discovers them (the address itself, its
  `/.well-known/caldav`, `current-user-principal`, `calendar-home-set`; Basic and Digest
  authentication; the password goes to the account's server only, or to another host of its site
  over https: iCloud's `p…-caldav.icloud.com`) and lists each calendar with "Events in the
  calendar" (ticked when it has events) and "Tasks in the notes" (ticked for task-only lists) and
  the page its tasks would go to. An account then has: its calendars (each with both switches and
  its page), "Changed on both sides: Keep the newer / Keep both", "Sync: 5 min / 15 min / hourly",
  "Remove the account" (asked first; nothing is deleted on the server or in the notes).
* **Syncing** — one `SyncScheduler` drives every calendar of the notebook (`caldav::CalendarSync`):
  ~1.5 s after the notebook opens, at the shortest interval of the accounts and subscriptions (the
  subscriptions themselves only when due), ~20 s after an edit of a page with synced tasks (the
  calendar's page, or a note holding a synced task's id), when the network comes back, when the app
  comes to the foreground (once five minutes passed), and by hand ("Refresh all", a row's ↻, the
  palette's "Refresh calendars"). A run: the events changed in Brulion are sent (the outbox), the
  server's changes fetched (`sync-collection`, else every member's ETag compared), the tasks
  reconciled with the notes field by field (docs/SYNTAX.md §5 "Tasks synced with CalDAV"),
  everything logged. Android's background check runs the calendars that sync tasks too (docs/
  ANDROID.md); a lock in the device state keeps the app and the check apart.
* **The calendar view** (`qml/views/CalendarView.qml`, `CalendarModel { events:
  Workspace.calendars }`): events among the notes' entries, in their calendar's colour (or the
  event's own `COLOR`) — month pills with a stripe of the colour, the week grid's blocks as long as
  the events last (an event going on past midnight to the day's end), all-day and multi-day events
  on every day they cover, the agenda's and the day panel's rows with a dot of the colour, the time
  they end ("until 15:30"), the calendar and the place; on narrow month cells one `calendar-clock`
  mark. Times are converted to the device's zone (time zones of the server, the feed's own
  `VTIMEZONE`). A series' exceptions are shown as the server keeps them: an override with its
  `RECURRENCE-ID` replaces that occurrence, one with `RECURRENCE-ID;RANGE=THISANDFUTURE` (RFC 5545
  §3.8.4.4) that occurrence and every later one — their properties come from the override, their
  start and end are moved by as much as it moved the instance it names, on the series' own wall
  clock (an hour later stays an hour later across a daylight-saving change; a UTC series shifts in
  UTC); a plain override of one later instance beats the range, `EXDATE` still removes an
  instance, a cancelled range ends the series there, and a range naming an instance the series does
  not have shows at its own start and gives its properties to the later ones. `RANGE=THISANDPRIOR`
  (deprecated) is one instance. A range override with an `RRULE` of its own (a series split in one
  component) replaces the series from the instance it names with its own instances — from its
  `DTSTART`, without its `EXDATE`s — until the next range begins. The header's calendars button (`calendar-range`) lists the calendars with their
  colours, each ticked when shown (a click hides or shows it: `setCalendarVisible`), "New event…"
  (when a writable CalDAV calendar shows events), "Refresh calendars" and "Manage calendars…" (the
  sync center's tab; "Add a calendar…" while there are none).
* **Reminders of events** — the `VALARM`s of an event of a CalDAV calendar or of a subscription ring in Brulion like a
  note's `⏰`: `caldav/eventalarms.h` turns them into notifications
  (`eventNotifications()`; `WorkspaceCalendars::eventAlarmsBetween()` keeps the scheduler's window
  until the events change) that `ReminderScheduler` takes as its extra source
  (`setExtraSource()`), so they are handed to the platform's sink with the notes' reminders
  (desktop notifications, Android's `AlarmManager`, the browser), fired in-app, caught up after the
  app was closed and recorded as missed ("Missed" lists them like the others), refreshed after
  every sync that changes the events (`extraChanged()`). Every occurrence of a series has them
  (an override's own `VALARM`s replace the series'); the first five of an event count. A `TRIGGER`
  relative to the start (or, `RELATED=END`, the end) rings that long before or after it — exact
  time, so eight hours before 06:00 on the morning the clocks go back is 23:00 the evening before —,
  a trigger in days on the wall clock, one further than 31 days is ignored; an absolute `TRIGGER`
  rings once and only for an event that does not recur; `ACTION:DISPLAY` (or none) is a
  reminder, `AUDIO` a loud alarm (`⏰!`), `EMAIL` and the like ring nothing. The notification's
  title is the event's, its body "Calendar · today 17:00 · place"; tapping it opens the calendar
  at the event's day (its path is `calendar:<day>`, not a note: nothing to snooze or mark done,
  the app lock's "Hide the note's text" says "Reminder" as for notes). Android's background check
  arms them too, from what the last sync kept (`<device state>/calendars`; it does not sync
  calendars that only show events). Each CalDAV calendar has "Ring the reminders of events" (the
  Calendars tab, on while the calendar shows events; `alarms` in its settings, on by default), and so
  has each ICS subscription — off by default there (someone else's calendar, a school's or a team's,
  need not ring on this device), its events' alarms read from the feed as last fetched. The event dialog's "reminder" choice writes the `VALARM` they use.
* **An event** (`qml/planning/CalendarEventDialog.qml`, a click or right-click on an event): its
  title, when ("Fri, Nov 20, 14:00–15:30"), its calendar with the colour, "Read-only" for
  subscriptions and calendars without the write privilege, the series ("every week", or the rule as
  written), the place, the description and "Open link". An invitation (an event with `ORGANIZER` /
  `ATTENDEE`s) shows "Organized by …" and each person invited with their answer (Accepted, Declined,
  Maybe, Delegated, No answer yet; the account's own in bold); when one of them is the account (its
  addresses: `calendar-user-address-set`, kept when the account was added) and the calendar is
  writable, "Your answer" — Accept / Maybe / Decline — sets that attendee's `PARTSTAT` for the whole
  event or series (`WorkspaceCalendars.answerEvent`, an `EventIntent::Kind::Answer` in the outbox;
  the server's scheduling, RFC 6638, sends the organizer the reply). "Note in the journal" appends `- 14:00–15:30
  Title (Calendar)` (with `location::`) to that day's journal. In a writable CalDAV calendar: "Edit"
  (title, all day, start and end — dates from an inline date picker, 24-hour times —, repeat: does
  not repeat / every day / every weekday / every week / every 2 weeks / every month / every year,
  a reminder: none / at the start / 5, 15, 30 minutes / 1 hour / 1 day before, place, description;
  for a series "This event / All events": one occurrence becomes an override with its
  `RECURRENCE-ID`) and "Delete" (a series asks "This event" — an `EXDATE` — or "All events"). "New
  event" picks the calendar among the writable ones. Changes show at once (the calendar's cache) and
  are sent by a run that starts then; when the server changed the event meanwhile, the account's
  policy decides (keep the newer by `LAST-MODIFIED`, or keep both — the edit as a new event). What
  the dialog changed is all that changes (caldav/eventchange.h, the same decision `brulion-cli`
  takes): a series edited from one of its occurrences keeps its own start unless its times were
  changed, and a series moved keeps its left-out occurrences (`EXDATE`) and exceptions
  (`RECURRENCE-ID`) on the occurrences they named.
* **Changes from the command line** — `brulion-cli calendar add | edit | delete` and the MCP tools of
  an AI agent (docs/CLI.md "Calendar events") leave each change as a file in
  `<device state>/calendars/inbox/`; the app watches that folder while the notebook has CalDAV
  calendars (not in the browser) and takes a new file about 0.1 s later under the calendars' lock
  — the change is applied to the event as the app has it then, shown at once and sent by a run
  that starts then, like the dialog's —, and every run takes what is waiting as it starts (a change
  left while the app was closed, or while Android's background check held the lock). A change that
  cannot be made (its event deleted on the server meanwhile, its calendar removed, a file that is
  no change) is dropped with an error in "What happened": "A change of an event asked for from the
  command line or by an AI agent could not be made." and the reason as its detail.
* **The sync center's Calendars tab** (`qml/io/CalendarsPanel.qml`): the state ("Calendars updated
  today 10:32", offline, what failed), "Refresh all", deletions of synced tasks held back by the
  guard with their titles and "Delete them", a word on CORS in the browser; the subscriptions — the
  colour (a menu of the palette), the name, when it was fetched or why not, ↻, 👁 (shown in the
  calendar or not), 🗑 ("Unsubscribe", asked first), "Fetch again: hourly / every 6 h / daily", a
  field to subscribe to another; each account's card; "What happened" — the log, newest first
  (updated feeds with their number of events, "3 received, 1 sent, 0 deleted", conflicts with how
  they were decided, deletions waiting, errors in words), "Clear".
* **Toasts**: "%n task(s) changed both here and on the server — see the sync center", "%n
  deletion(s) of synced tasks wait for a confirmation in the sync center".
* **In the browser** the requests are the browser's: a server must answer with CORS headers for
  the page's origin (docs/WEB.md "Calendars in the browser"); a refused fetch says so.

## Shared components (`qml/components/`)

`BButton`, `BIconButton`, `BTextField`, `BSuggestField` (a field with a suggestion list: key and value suggestions of the properties panel), `BTextArea`, `BSearchField`, `BSwitch`, `BSlider` (a number on a track: `committed(value)` once a drag or a key ends — extension views' sliders), `BCheckBox`,
`BChip` (given less than its implicit width — an explicit width, a layout shrinking it — it elides its text; chips in a `RowLayout` that may be narrower than they are go into a `Flow` filling the row (it wraps them, `width: Math.min(implicitWidth, flow.width)` elides one wider than a line), as a layout never shrinks an item without `Layout.fillWidth` below its preferred width and widens itself instead — the AI chat's header once pushed the whole conversation past a phone's edge), `BBadge`, `BTabBar`/`BTabButton` (`picked(index)`: the user's choice of a tab — a tap, a click, Space, the arrow keys —, for tabs that stand for a route; not every change of `currentIndex`, which also comes as tabs are added or removed), `BSegmented`, `BBreadcrumb`, `BCard` (`elevation`,
`tone`, `clickable`; `flat`: no surface, border, shadow or padding — settings groups on phones),
`BListItem` (`subtitleInline`: the subtitle after the title on the title's baseline, the row one
line high — the floating palette's rows; `iconEmoji`: an emoji in the icon's place — a page's icon),
`BPageIcon` (a page's emoji or app icon, a fallback icon without one),
`BSectionHeader`, `BKbd`, `BText`, `BDialog` (bottom sheet on narrow windows),
`BConfirmDialog` (`subject`: the item asked about, framed above the message), `BMenu`/`BMenuItem` (`swatch`: a colour square in the icon's place — the highlight
colours)/`BMenuSeparator`, `BToast` (+ `BToastHost`; the message is
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
quick picks: in 10 min, in an hour, today 19:00, tomorrow 8:00, Monday 9:00),
`BColorSwatch` (a round colour button: selection ring and check mark, tooltip),
`BAccentDialog` (a custom accent: colour grid, `#rrggbb` field, live preview of what
`ThemePalette` makes of it), `BFontPickerDialog` (installed font families with fuzzy search, each
row in its own font, the default first), `BDiffView`,
`BMergeView` (per-conflict choice: Mine / Theirs / Both / Manual), `BRichText` (rendered note text
outside the editor's blocks: rounded tags, cut after `maximumLineCount` lines with "…"),
`BLinkPreview` with the singleton `LinkPreviews` (the hover preview of links, "The block editor"), `BPalette` with `BCommandPalette` and
`BQuickSwitcher` (floating on wide windows: one-line rows, a subtitle such as a page's folder after
the title, over `Theme.scrimModal`, the list as many whole rows high as fit — measured from the
rows the view has created, a row it released after scrolling counting as one of those, so the
list keeps its height however far it scrolls; full screen on compact ones: two-line rows over
`Theme.scrim`), `BKeyboardFocusKeeper` (the focused form field above the on-screen keyboard,
"On-screen keyboard"), `BKeyboardPlaceholder` (a simulated keyboard's grey block). Icons: SVGs in
`assets/icons/` (Lucide, ISC licence) via `BIcon`.

## Strings

`qsTr()` everywhere with English source text; Polish translation in `i18n/brulion_pl.ts`
(English is the default UI language — whatever the system's locale —, Polish is chosen in the
intro or the settings; `brulion_en.ts` only holds English plurals). Plurals with
`qsTr("%n notes", "", n)`. The language can be switched at run time (settings, palette).
Qt's own texts (dialog buttons, `FilePicker`'s fallback widget file dialog) come from Qt's
catalogs `qtbase_pl.qm` / `qtdeclarative_pl.qm`, bundled as resources (`QtTranslations` in
`src/app/ui`; nix: `qt6.qttranslations`) and switched with the language. The system's own file
pickers (Android, Windows, GTK / KDE, the portal, the browser's) speak the system's language.

## Design system in code (`src/app/qml/theme`, `src/app/qml/components`, `src/app/ui`)

* **Theme** (`Theme` singleton): every colour, font, size, radius, duration comes from it —
  semantic colours (`bg`, `bgElevated`, `bgSunken`, `fg`, `fgMuted`, `fgFaint`, `border*`,
  `guide` / `guideOnSunken` (indent guides; aliases of the border tokens),
  `accent`, `link`, `tag*`, `chip*` via `Theme.chipBg(kind)` / `chipFg(kind)` / `chipIcon(kind)`,
  `selection`, `focusRing`,
  `danger*`/`warning*`/`success*`, `diff*`), typography (`fontUi`, `fontEditor`,
  `fontMono`, `text*` sizes), spacing `space1…space16`, radii, `elevationBlur(level)` /
  `elevationOffsetY` / `elevationSpread` / `elevationOpacity`,
  durations (0 with `reducedMotion`), `touchTarget` and control heights. Paragraph line
  heights are fixed pixel heights so they look the same with every font:
  `lineHeightMode: Text.FixedHeight; lineHeight: Theme.lineHeightPx(font.pixelSize, Theme.lineHeight)`
  (a proportional `lineHeight` multiplies the font's own spacing, 1.15–1.4 em depending on the
  font). White text goes on `danger`/`accentSolid`/`primary` fills only — the brand red
  `accent` is for lines, dots and focus marks (3.7:1 with white). The surfaces (`bg*`,
  `border*`), the accent family and the text colours come from **`ThemePalette`**
  (`src/app/ui/themepalette.h`, library `brulion_ui_helpers`, QML singleton):
  Theme's `palette` is a `ThemeColors` (the same file: `ThemePalette.colors(dark, lightSurface,
  darkSurface, accentName, customAccent)` as typed properties, `Theme.colors` its map); `accentBg` is the accent at 12 % / 18 %. `tst_ui_themepalette` checks every
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
  **Overrides** (docs/EXTENSIONS.md "Theme": a theme page picked in Settings → Appearance →
  Themes, an extension's `b.app.theme`, an AI agent's live theme): every token of the catalog (core
  `extensions/appapi.cpp` `themeTokens()` — the colours, `radiusXs` … `radiusXl`, `focusRingWidth`,
  the spacing unit `space` — `Theme.spaceUnit`, 4 px: `space1` … `space16` are its multiples —,
  `fontUi` / `fontSans` / `fontSerif` / `fontMono`) can be replaced. `AppScripting` merges the layers into `ui::ThemeOverrides` (one per process, a map per
  mode); `ThemeColors` puts them over its own palette tokens (`fgOnAccent` is its `accentOn`) and
  exposes the mode's map as `palette.overrides`. Every other colour token of Theme.qml keeps its own
  value as `_<name>Base` and reads `_pick("<name>", _<name>Base)` — the override as text through
  `palette.pickText()`, a string bound to a `color`: a C++ method's `QColor` result cannot be stored
  in a compiled `color` ("Compiled QML") —, the radii, `focusRingWidth` and `space`
  `palette.pickNumber(palette.overrides, "<name>", <value>)`, the fonts `_pickFont()` (an installed
  family only, else the token's own). With no overrides each token is exactly its own value
  (`tst_ui_themepalette`); derived tokens follow the ones they are made of (`accentBg` an
  overridden `accent`, `guide` an overridden `border`) unless overridden themselves. A new colour
  token of Theme.qml goes into the catalog too (and the docs' list), or it cannot be overridden.
* **Components** are built on `QtQuick.Templates` (no style dependency; the few files that need
  a ready control import `QtQuick.Controls.Basic`, never `QtQuick.Controls` — its run-time style
  selection packages all six styles, and configure refuses it, "File pickers"), each file documents
  its API at the top. Conventions: icon-only buttons need a `label` (accessible name +
  tooltip); every interactive control shows `BFocusRing` on keyboard focus; on touch UIs
  hit areas are ≥ 44 px (visuals stay compact via insets) and hover effects are off;
  dialogs/popups are asynchronous (signals, never `exec()`); note text is never rendered
  as rich text unless escaped (`TextHelper.markMatches`, `DiffHelper.segmentsHtml`).
  The components that show names are plain text (`textFormat: Text.PlainText`): `BText`,
  `BListItem`, `BMenuItem`, `BBreadcrumb`, `BToast`, `BChip`, `BDialog`'s title and subtitle,
  `BConfirmDialog`'s subject, message and detail, `BToolTip` and `BEmptyState` — they quote page,
  folder, file and tag names and typed searches, which come from anywhere (sync, imports, a file
  named `<b>Ważne <i>notatki.md`). With Qt's default `AutoText` a name holding a tag turned into
  styled text: a crafted name in the trash confirmation could pass for another name, or for none.
  A confirmation about one item names it in a frame of its own (`BConfirmDialog.subject`: the
  name as written with the item's icon, on a sunken surface above the message, which then says
  "It will be moved to the trash…" without the name) — the trash confirmations of the tree, the
  top bar's More, the page menu and All pages, and Trash's "Delete for good?": on its own line a
  name cannot read as part of the app's sentence.
  Callers pass names as they are — escaped they would show `&lt;`. (`BButton`'s and the other
  controls' labels are still `AutoText`: a name there is escaped by its caller, e.g. the tag
  view's parent tag.)
  Context menus open with a right-click and, on touch, a long press (`onPressAndHold` /
  `onLongPressed`); a right-click `TapHandler` also sets `acceptedDevices: PointerDevice.Mouse |
  PointerDevice.TouchPad | PointerDevice.Stylus` — Qt hands finger touches (no buttons) to a
  TapHandler whatever its `acceptedButtons`, so without it every tap opened the menu.
  A long menu of a page (the top bar's ⋮, the page's ⋯) sets `BMenu.sheet`: on touch UIs it is a
  sheet from the bottom edge instead of a popup at its button — the window's width, `radiusSheet`
  at its top corners with a grabber, the page dimmed behind it (`Theme.scrim`), rising from the
  edge, placed again as its items grow (`placeAsSheet()`), above the on-screen keyboard and the
  navigation bar's inset —; with a mouse it stays a menu.
  Every menu is walked by keyboard the same way (`BMenu`, Qt's `T.Menu`): it opens with no item
  highlighted (the task status menu: at the task's status), ↓ starts at the first item shown,
  ↓ / ↑ move over the items shown (hidden and disabled ones skipped, no wrapping at the ends),
  Enter, Return or Space trigger, → / ← open and close a submenu, Esc closes, and the focus goes
  back to what had it (an edited text keeps its selection). `BMenuItem` takes the focus from Tab
  only, and only while shown — `T.Menu` moves the highlight only to items that do and does not
  look at visibility; with `Qt.NoFocus` the arrows did nothing. So no menu has key handlers of its
  own. What is no `BMenuItem` (the More menu's page tiles and Mode switch) is reached with Tab.
  Destructive actions follow one rule (`BMenuItem`'s header): an item that deletes is the last of
  its menu or section, with a separator above it (and below it when sections follow: the More
  menu's "Move to trash" before Command palette, Notebook and Settings); `danger` (red) only for
  deleting content or files — the trash, a block, a table's row or column, a reminder, a drawing's
  backdrop, the sync history, the event log —, never for the "none" value of a choice list (No
  status, No priority, Don't repeat are choices like the others: no red, no ×); a clear that
  cannot be undone asks first, its text ending with "…" (`BConfirmDialog`: "Clear the log…",
  "Clear history…", "Restore the default look…").
  Settings cards (`SettingsView`'s groups, the sync section) divide their rows with 1-px
  `borderSubtle` hairlines (`ShellSettingDividers`, drawn over the card's column: none under a
  row without controls that titles what follows, none above a plain note); every title — a
  switch's too — starts at the card's one left edge; a control sits at the row's right edge,
  vertically centred on the title and description, as the switches do. On compact windows
  (< 600 px) a row puts its control under the text only when it does not fit beside it
  (`ShellSettingRow.stacked`: the title, a gap and the controls' natural widths —
  `naturalWidth` of `BSegmented` and `BButton`, which `fill` never changes — wider than the row,
  the text left under 40 % of it, or a wrapping group such as the accent swatches, or a text
  field); each row decides alone (at 412 px Light background, Dark background and Interface font stay
  beside their text, Theme, Text size and the swatches go under it; at 360 px Dark background
  goes under too; a long translation stacks by itself). A wider window stacks a row only when
  its controls would leave the text a sliver — under 180 px (`minTextWidth`): the AI provider's
  three choices in a narrow pane, the sidebar open in a small window. A wrapping group fills the row up to
  `ShellSettingRow.evenWrapWidth()`: the accent swatches are one line wherever they fit (the
  seven presets and "Custom…": 8 × 44 px at 412 px, the intro's seven), else even lines (4 + 4 at
  360 px, not 7 + 1). Under its text a control fills the row:
  a segmented control (its segments as even as their labels allow: the narrow ones share one
  width, none is cut) and a lone framed button (one beside a badge or another button, and a text
  button, keep their width), so every row ends at the same right edge. A segmented control's selected segment
  is a raised thumb on the sunken track: the raised surface in light; in dark that surface
  lightened by 12 % of the text colour with a `borderControl` outline, clearly lighter than the
  track. The track has a quiet `borderSubtle` frame in light (the control keeps its shape on
  the paper page) and none in dark: there the well reads against the card and the selection's
  outline is the control's only one. An off `BSwitch` is a solid `borderControl` track (≥ 3:1
  on `bg`, `bgElevated` and `bgSunken`, WCAG 1.4.11; `fgFaint` under the mouse), an on one the
  `link` colour; its knob says the state as well (WCAG 1.4.1): a Lucide check when on, a cross when
  off, in the track's colour (12 px in the 18-px touch knob, 10 px in the 14-px one; on: `link`
  on the knob 5.7:1 in light and 8.7:1 in dark, off: `borderControl` 3.5:1 and 3.4:1); the
  gallery's "Podstawy" checks both marks live and shows both states ("STANY KONTROLEK (≥ 3:1)").
  A problem with what is typed shows at that field (`BTextField.errorText`: a red border, the
  message under it, its accessible description) as soon as it is known, the accept button
  disabled; a dialog's subtitle says what accepting does, never what is wrong (the rename and
  new folder dialogs, the accent and recurrence dialogs). Enablement and previews read
  `typedText` ("On-screen keyboard").
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
* **Following changes**: a binding follows the narrowest counter that covers what it reads —
  Zed-like targeted notifications, not one global counter. `Workspace.revision` grows with every
  change of a file, an edit of a page's text included: every autosave while typing re-runs what
  follows it, also in the views `ShellRouteView` keeps hidden. So only what reads the notes'
  content follows it (the side panel's calendar dots and day plan, the calendar grids, Settings'
  checks of the configuration). `Workspace.namesRevision` grows when the set of names may have
  changed — a file or folder created, deleted or renamed, aliases (the index's
  `changesSince().names`), open / close, the configuration, the index read: `exists()`,
  `journalPath()`, `journalDates()` / `journalMarks()`, `subfolders()`, `switcherPages()`,
  `pictureAttachments()`, the folder choices, `periodicNoteOf()`, and `pageCount` notify with it.
  `Workspace.propertiesRevision` grows when some changed note's page properties differ from
  before (compared in `onVaultChanged()`, O(changed notes)): `pageIconOf()`, `tasksIgnored()`.
  Cheap tests come first and gate the read (`switcher.visible && Workspace.namesRevision >= 0`,
  `days.count <= 1 && Workspace.pageCount <= 1`, a dialog's `visible`): a binding whose condition
  stops before the counter does not subscribe to it at all. A hidden kept view that must keep its
  rows as they were (scrolled where it was) holds the counter in a property that a
  `Binding { when: root.visible; restoreMode: Binding.RestoreNone }` updates only while shown (All
  pages' folders, `subfoldersAt`). C++ helpers do the same: `PageTools`' header summary is
  computed again only for changes it shows, an edit of the page itself on the next event-loop
  turn (pagetools.h), lists
  follow `fileListChanged()` or `IndexChanges` (`WorkspaceListModel.active`, `affectedBy()`).
* **Text formats**: every `Text` and `Label` says how its text is read. Text that comes from a
  note — a page's title, a task, a file name, a line in the merge view, what an extension
  wrote — is shown with `textFormat: Text.PlainText` (`BText` and the other `B*` components do
  that themselves); markup that Brulion makes, with the note's text escaped (`InlineRenderer`,
  `TextHelper.escapeHtml`), says `Text.StyledText` or `Text.RichText`. Qt's default,
  `Text.AutoText`, decides by the text: one with a tag before its first line break is styled, so
  a title `<img src="file://server/share/x.png">` made Qt fetch the picture (on Windows from
  another machine, with the user's sign-in — the engine's URL guard does not see a picture inside
  rich text) and `<font size=7>` redrew the row; it also costs a scan of every string.
  `scripts/check-qml-text-format.sh` finds a `Text` or `Label` without a `textFormat` (ctest
  `tst_scripts_check_qml_text_format`, CI's lint job).
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
* **`SmoothScroll`** (singleton; `main()` installs it as an application event filter): the mouse
  wheel and the scroll bars' paging scroll like a web browser in every Flickable ("Scrolling
  performance" → "Wheel and scroll bar"); `SmoothScroll.scrollBy(flickable, dx, dy)` scrolls the
  same way from QML. Static library `brulion_ui_smoothscroll`, tested by `tests/app/smoothscroll`.
* **`NearView`** (attached, `NearView.near`), **`PictureDecode`** (singleton), `MemoryKeeper` and
  **`MemoryPolicy`** (singleton): pictures hold their pixels only near the window's view, decoded
  for the place that shows them, freed memory goes back to the system ("Scrolling performance" →
  "Pictures hold their pixels only near the view"), and what the app keeps for speed goes while
  it is away ("Navigation" → Away). Static library `brulion_ui_memory`, tested by
  `tests/app/memory`.
* **`TouchFlick`** (attached, `TouchFlick.enabled: true` on a Flickable / ListView): touch flicks
  at the finger's speed on Android ("Scrolling performance"); static library `brulion_ui_touch`
  (Qt Quick, no private Qt headers), tested by `tests/app/touch`. As an attached type it carries
  `QML_ATTACHED` itself (Qt resolves `qmlAttachedProperties` on the registered class), the app's
  wrapper in `qmlforeign.h` gives it its QML name.
* **Icons**: Lucide SVGs in `assets/icons/` (see its LICENSE), `BIcon { name: "bell" }`. One icon,
  one meaning in the frame: `book-open` is the journal (the rail, the bottom navigation, tabs, the
  palette's Journals), reading mode is `eye` (the top bar, the page menu, the palette; leaving it
  `pencil`), the documentation `circle-help`, opening something in the main view `external-link`
  (the journal day's "Open as page", the side panel's "Open in the main view"), `arrow-left` is
  only Back and `panel-left` only the sidebar (a page window's "Move to main window":
  `square-arrow-down-left`); in the drawing editor `square` is only the rectangle (plain paper:
  `file`) and `copy` only Copy (Duplicate: `copy-plus`).
  New icons: copy from `lucide-static`, normalise like the others, re-run CMake. They render
  everywhere, the browser included (the SVG plugin is linked statically there; `BIcon` recolours
  on the CPU, no shader effect).
  `BIcon` is a C++ item (`src/app/ui/iconprovider.h`, library `brulion_ui_icons`; until 2026-11
  `BIcon.qml`, an `Item` with an `Image` and nine bindings) that makes one Qt Quick `Image` as its
  child the first time it is visible with a name, and sets its properties itself — no bindings, no
  `Accessible` attached object (icons are decorative: not accessible items), no QML context of its
  own; its `size` and `color` are `Theme.iconSize` and `Theme.fg` until set. Not an `Image`
  itself, whose implicit size is read-only and its picture's: the image's source is
  `Icons.sourceUrl(name, color)`, "image://bicon/<colour>/<name>", whose
  provider rasterises the SVG at the requested size (size × the device pixel ratio) and fills it
  with the colour in `SourceIn` — exactly what Qt's `IconImage` drew; Qt Quick's pixmap cache then
  shares one image and one texture among every icon of that name, size and colour (an
  `IconImage` recoloured and uploaded each one on its own). Like `IconImage` it draws the picture
  at its own size, centred, in a larger box and scales it down in a smaller one; its implicit size
  is its width and height, as before. Creating the `Icons`
  singleton gives its engine the provider. `tests/app/icons` compares it with `IconImage` pixel by
  pixel at device pixel ratio 1 and 2.
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
    defaultSuffix: "zip"                   // SaveFile: "Note" → "Note.zip"
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

## Compiled QML

Every binding, signal handler and function of the app's QML is compiled to C++ ahead of time
by qmlcachegen (Qt Quick Compiler); none is left to the QML interpreter. ctest
`tst_qml_compiled` reads qmlcachegen's statistics of the build and lists anything that is not
compiled, with qmlcachegen's reason, and anything that took it longer than 30 seconds to compile
("Short functions" below); builds with `BRULION_WERROR` (`scripts/dev-build.sh`, CI) fail on it
right after linking the app:

```sh
cmake --build build --target brulion
cmake -DLIST=build/src/app/.rcc/qmlcache/module_brulion.aotstatslist -DSOURCE_DIR=$PWD \
      [-DFILTER=/qml/editor/] [-DREPORT_ONLY=ON] -P cmake/BrulionQmlCompiledCheck.cmake
```

The statistics are the per-file `.aotstats` that qmlcachegen writes with `--dump-aot-stats`
(`all_aotstats` prints Qt's own summary). Qt 6.11 writes them for `qt_add_qml_module()`'s files only;
`src/app/CMakeLists.txt` turns them on for the areas' `qt_target_qml_sources()` too. Only the first
reason of each function is listed. A file is compiled again only when it changes: after changing
a component's API, `touch` the files that use it before reading the list.

The generated C++ (`.rcc/qmlcache/brulion_*_qml.cpp`) is compiled with `-Oz` in optimised builds
(`BRULION_QML_CODE_FOR_SIZE`, `src/app/CMakeLists.txt`): at `-O3` it was half of the app's machine
code, and being glue around engine calls it measured no slower for size (docs/PERFORMANCE.md, L20).
Work that has to be fast belongs in C++ helpers anyway, which keep the build type's optimisation.
Android and browser releases also leave the QML source text out of the resources
(`DISCARD_QML_CONTENTS`). Everything below was
verified with Qt 6.11.2 (6.12 compiles the same).

**Types everywhere.** Functions get annotated parameters and return types
(`function f(text: string, at: int): bool`, `: void`; no default values — a call passes every
argument); signal handlers name their parameters (`onX: (a, b) => …`). Properties have their real
type, not `var`: `list<string>`, `list<var>`, `list<int>`, `rect`, `point`, a component or C++
type. `pragma ComponentBehavior: Bound` in every file, ids of outer components qualified
(`root.x`).

**JavaScript built-ins.** String and Number methods (`s.trim()`, `indexOf`, `slice`, `split`,
`replace`, `startsWith`, `toLowerCase`, `s[i]`, `n.toString()`, `toFixed`, `String(n)`,
`Number(s)`, `parseInt`, `isNaN`), regular expression literals, `JSON.stringify`, `Date.now()`,
`Math.atan2` do not compile: the singleton `Js` (`src/app/ui/jshelper.h`, tested against the
JavaScript engine) has each of them with the same result (`Js.trim(s)`, `Js.indexOf(s, x)`,
`Js.test(s, "^\\d+$", "")`, `Js.replaceRegExp(s, "\\s+", "g", " ")`, `Js.dateNow()`). The QML
engine has no `replaceAll`, `trimStart`, `trimEnd` at all (`Js` has). What compiles: `s.length`,
`===`/`!==` (not `==`), `+` (`"" + n` and `"" + url` give the text; `color.toString()` too),
template strings, `qsTr().arg()`, `qsTranslate()`, other `Math.*`, `Qt.formatDate`,
`Qt.platform.os`, `Application.state` / `Application.layoutDirection` (not `Qt.application.*`),
`switch`, `x as Item` with `!== null` (instead of `instanceof`).

**Lists.** `push`, `concat`, `splice`, `sort`, `reverse`, `filter`, `map`, `forEach`, `find`, an
assignment to an element of a local do not compile. `Js.appended`, `Js.concat`, `Js.removedAt`,
`Js.sortedStrings`, `Js.fromStrings`… for simple cases; anything more (sorting by a key, filtering,
grouping, building rows) belongs in the C++ that produces the data — controllers, models, the
areas' helper singletons (`UiModels`, `BlockViews`, `PageTools`, `IoLists`). What compiles: `length`,
`[i]`, `indexOf`, `includes`, `join`, `slice`, `for (let i = 0; i < l.length; ++i)`,
`list[list.length] = x` on a `list<…>` property (not across `if`/`return` branches). An array
literal compiles only into a `list<…>` property or return value: not as a call's argument, not
inside an object literal, not as `cond ? x : []` (`if (!cond) return []`; a
`readonly property list<var> noRows: []`). Neither does an object literal in a conditional,
`cond ? { … } : { … }` (also `: {}`): qmlcachegen stores the first branch's map in a register the
result is not read from, so it gives an empty value when `cond` holds (the page icon dialog's emoji
were blank) — `if`/`else`, or one object literal with the conditions inside its fields
(`({ kind: e !== "" ? "emoji" : "app", name: … })`). `QVariantList` converts to `list<var>`, not to
`list<int>`; `QStringList` not to `list<var>` (`Js.fromStrings`).

**No functions as values.** Arrow functions and `function () {}` as values (`array.map(x => …)`,
`Qt.callLater(f)`, `Qt.binding(() => …)`, `connect(f)`, `nextCheckState: () => …`, a callback
argument) do not compile; neither do `try`/`catch`, `for…of`, `for…in`, `typeof`, `delete`,
`arguments`, `ScriptAction { script: … }` (it also makes qmlcachegen skip the rest of the file —
the animation's `onFinished:` instead). Instead:

* a deferred call: `Later { onTriggered: … }` (`src/app/ui/later.h`), `restart()`ed — the same
  posted event as `Qt.callLater`, not a timer (a `Timer { interval: 0 }` runs after the timers
  already due: on slow machines the editor's scrolling after a zoom came after a self-test's next
  step); repeated requests are one call, with the last values (unlike `Qt.callLater` with
  different closures); the handler checks that its owner is still alive
  (`BlockEditor.laterEnsureEditVisible()` / `laterRevealRow()`). Whether a posted event runs
  before or after a timer due at the same time is the platform's choice (Windows: not always
  before): a self-test step that checks the outcome of a deferred call waits for it
  (EditorPlayground's `waitStep()`), it does not count on the next tick;
* a callback: a signal, or a target name the receiver hands back (`EditorDialogs.pickDateWith(target,
  …)` → `applied(target, action, a, b)`), or an id (`BToastHost.showToast()` returns one,
  `actionTriggered(id)`; `AppController.toastWithAction()` / `toastRestore()`);
  `OpenIn.begin(target)` … the navigation … `OpenIn.end()` instead of a function to run;
* a binding set after a click: a declared binding — `Binding on checked { value: … }` or a counter
  (`checked: menu.checkRevision >= 0 && …`, `++checkRevision` on click); assigning `checked = …` in
  JavaScript removes the binding;
* a ListModel's `insert` / `remove` / `get`: a C++ model (`ToastListModel`).

**Reading members of untyped values.** A member of anything typed `var` does not compile: a
JavaScript object, a `QVariantMap` / `QVariantList` held in a `var` property, parameter or local,
`modelData` of an array model, `Text.fontInfo`. Instead:

* delegates take each key as a typed `required property` (`required property string title`).
  Every row must carry every key the delegate requires (a missing string arrives as "undefined", a
  missing `list<…>` fails "not initialized"). Required properties cannot be named like the
  delegate type's own properties: `left`, `right`, `top`, `bottom`, `horizontalCenter`,
  `verticalCenter`, `baseline` (Item's FINAL anchor lines — the component does not load, and with it
  the app), `display`, `icon`, `text`, `state`, `color`, `count`, `id`, `index` — `required text` marks
  the existing property required, or the producer uses another key, or the delegate is wrapped in
  an `Item`;
* a `FINAL` `Q_PROPERTY(QVariantMap …)`'s keys compile (`Workspace.sync.status.state`), also in
  arithmetic; `MapHolder { map: … }` (`src/app/ui/mapholder.h`) is such a property for any map,
  one level per holder: `holder.map = controller.copyText(t); holder.map.ok`. Write a holder only in
  handlers and functions called from them — written from a function a binding calls, it makes a
  binding loop;
* `AppController.routeText/routeInt/routeFlag/routeStrings/routeList(map, key)` and
  `UiModels.stringField/boolField/numberField(map, key)` read one value of a map (JavaScript's
  truthiness for `routeFlag`); a `Q_GADGET` value type (`QML_VALUE_TYPE`, `FINAL` properties)
  compiles everywhere.

**Results of methods ("shadowable").** A `Q_INVOKABLE`'s result, and a property of a non-`FINAL`
C++ property or of another QML component reached through a typed reference, may be replaced by a
subclass: passing it on, returning it and comparing with `===` compile; a member of it
(`TextHelper.fuzzyMatch(q, t).score`, `item.mapToItem(…).y`, `editor.controller.titleRow`) or `==`
does not. Instead: a `FINAL` C++ property; a `final property PageController controller` in the
owning QML file (`final readonly` does not help); a result that is what QML needs (the score
itself); launder it through a typed QML function of the same file (`function names(): list<string>
{ return Helper.names() }`, then `names().length`; for `string`, `int`, `bool`, `real`, lists — not
for `color`, `point`, `rect`, gadgets) or a typed property (`root.at = item.mapToItem(…); root.at.y`;
`mapToItem` through the file's root item compiles directly); `x as T`; `"" + x` / `0 + x`. A method
returning `QColor` cannot be stored in a `color` (`"" + call()` can, or a FINAL property); one
returning `QPointF`/`QRectF` only through a typed property or a typed function's parameter.
Every new `Q_PROPERTY` is `FINAL`; methods cannot be.

**Side effects.** After any call, compiled code does not read again what the call might have
changed: a local holding a shadowable result, a `var` or list parameter (`list[i]`, `.x` of a
`point`/`rect` parameter, `eventPoint`'s fields), an element of a `var`, a local list in a loop that
calls methods — "argument affected by side effects" / "reading from a value potentially affected
by side effects". So read parameters and event fields at the start of a function into typed
locals, compute the arguments of one call before it (`const a = …; const b = …; f(a, b)`, not
`f(g(), h())` with shadowable results), read lists from properties (`property list<string>
lastTrashed`), pass a map through a `MapHolder`, or move the loop into C++.

**Colours.** `Qt.rgba()`, `Qt.hsla()`, `Qt.lighter()`, `Qt.darker()`, `Qt.tint()`, `Qt.alpha()`,
`Qt.color()` (they return `var`) do not compile: the `color` type's own methods do — `c.alpha(0.5)`,
`c.lighter(1.2)`, `c.darker(1.5)`, `c.tint(other)` (`alpha()` warns above 1 where `Qt.rgba`
clamped: clamp it), `c.r/g/b/a/hslLightness` reads — and a colour literal (`"#80ffffff"`), a string
bound to a `color`, `ThemeColors`' typed tokens (`Theme.palette`).

**Types of QML components.** A file cannot store its own root into another file's property typed
with the file's own type ("cannot convert from X to X"). `import Brulion as B` with `root as B.X`
compiles but **crashes at run time** (Qt 6.11.2): never use it. The receiver takes the base type
and casts in its own file — `property Item editorItem` + `final property BlockEditor editor:
editorItem as BlockEditor` — or has a setter `function setOwner(o: Item): void { owner = o as Ed }`.
Cyclic `required property` types the same way. `Drawer` and `Menu` as a declared type: `T.Popup`.
The children of an item (`item.children[i]`) do not compile: `readonly property list<Item> rows:
ref.children` for a property, else a property of the child (`AppController.propertyOf(item,
"day") as ContentJournalDay`).

**Files loaded when first used.** A QML file named as a type in another one — a child element, an
inline `Component { X {} }`, a `Loader.sourceComponent: X {}`, a property, parameter or return
type, an `as X` cast — is loaded together with it, and the engine keeps its compiled unit (rebuilt
on the heap), property caches and binding tables (docs/PERFORMANCE.md "QML files loaded when first
used"). What the first screen (the shell, the journal with ordinary blocks) does not show is never
named by what it shows: the views (ShellRouteView), the start screens (`Main.qml`), a day as a page
(JournalsView), the shell's and the editor's dialogs, menus and popups (`AppShell.dialogNow()`,
`BlockEditor.makePopup()`, `dialogsNow()`), the touch toolbar and the selection's tools, source mode,
the block kinds a page may not have (tables, calculations, queries, diagrams, charts, views,
properties) and a block's embeds and transclusions (their `BlockPart*.qml` made through `BlockParts`;
BlockDelegate reads their first line by name), a base's, a PDF's and a player's tile, the viewers of other files and the page's dialogs (PageView), the side
panel's content, the extensions' views in the sidebar, the self-tests and dev scripts. They are made
from their files: `Loader.setSource(url, values)` (the URL from `Qt.resolvedUrl()`, also in releases
without the QML source text — the compiled unit is found by its URL; the arguments read from
properties, "Side effects" above), or `Qt.createComponent(url).createObject(parent, values)` for an
object kept in a property. Their users reach them through a type that is loaded anyway (`Item`,
`T.Popup`, `BMenu`, `Window`), and:

* call their own functions and set their properties by name — `QmlCall.call2(dialog, "openFor", path,
  isDir)` (up to `call6`; `callFlag`, `callNumber`, `isTrue()`, `integer()`, `text()` type a result),
  `QmlCall.set(menu, "kind", kind)`, `QmlCall.get()` / `object()` (`src/app/ui/qmlcall.h`: each
  argument converted to the type its parameter declares, as `uidriver::callQml()` does);
* follow a property for a binding — `QmlValue { object: loader.item; name: "sheet" }`, `.flag` /
  `.number` / `.text`; or, without an object of their own per instance, `QmlCall.connect(item,
  "firstLineCenterChanged", block, "readLineCenter")`;
* take their signals with `Connections { target: loader.item; function onDone(path: string) … }` or
  `QmlCall.connect(sender, "signal", receiver, "function or signal")` — what a handler written in the
  inline component did moves into a typed function of the user;
* set what follows the user's state with `Binding { target: loader.item; property: …; restoreMode:
  Binding.RestoreNone }` (one object each: where many instances would each need one — a block's
  embeds — `QmlCall.set()` from a change handler instead).

A file that only moved out of another one (`ShellDeleteDialog.qml`, `JournalsJumpDialog.qml`,
`EditorMergeDialog.qml`) keeps its texts' translation context with `qsTranslate("<old file>", …)`. Dev
and self-test QML (loaded only when asked) may name anything: they cast what they read (`editor.blockMenu
as BlockMenu`). The memory scenario of the app bench (`tests/bench/app`, `BENCH_QML_TYPES=1`) lists the
files loaded at every step.

**Other.** `"\r"` inside a compiled string literal breaks the generated C++ (qmlcachegen writes a
raw CR): keep it in a property (`readonly property string crlf: "\r\n"`). `Qt.inputMethod` →
`DeviceInfo.inputMethodVisible` / `hideInputMethod()`. A font in a `?:` → a `Binding` element. A typed
`QVariantMap` parameter refuses `null` (the interpreter took it). Self-tests (`qml/dev`) drive the
UI through the C++ helpers `DevEvents`, `DevTest`, `DevBlock`, `DevRecord`
(`src/app/ui/devtesting.h`), C++ self-tests call typed QML functions through
`uidriver::callQml()`, which converts each argument to the declared type.

**Short functions.** qmlcachegen's time over one function grows steeply with its branches — every
`if` and above all every `&&` / `||` / `??` of its conditions: in Qt 6.12 about with their fourth
power (generated self-test steps with three `&&` each, on a Raspberry Pi 4: 12 steps 5 s, 24 steps
87 s; the same 24 steps in two functions 11 s, in four 2 s). A self-test with 157 steps in one
function took qmlcachegen 25 minutes on each CI runner (Qt 6.11: seconds), and the builds ran out
of time. Long sequences go into parts of at most about 20 steps and 20 short-circuit operators,
called in order — the playground's step functions do so (`desktopSteps()` calls
`desktopStepsPart1()` …): `at()` counts the steps across the parts, so a step's number does not
change. Constants the parts share are computed once in the calling function and passed as typed
parameters. The check above also fails on any binding or function that took qmlcachegen longer than
30 seconds (`-DBRULION_QML_MAX_COMPILE_SECONDS=<s>`, 0 for no limit) and names the slowest one in
every build.

### The QML engine's garbage collector

`main.cpp` sets `QV4_GC_TIMELIMIT=0` before the engine exists (a value the user set wins): the
collector does a whole run at a time, as before Qt 6.8, instead of 5 ms steps between other work.
Qt 6.11.2's incremental collector can free the JS wrappers and the storage of the QML properties
of live objects when its steps fall into the creation of a component. That was seen in
`DrawingView`'s top `DrawingToolbar`: its root has no parent until the whole file is created.
It also needs that nothing reaches the objects from JavaScript at the time (an `id` used only in
functions does not count). The objects stay on screen, but their properties read as empty
(`label`, `iconName`). A written `color` or object property warns "Cannot find member data" and
changes nothing: the pen kept its tint when another tool was chosen.

It happens only under CPU load: on CI runners, or when the app shares the processor. In 32
runs at a time on 16 cores, 5–18 % of the drawing's starts lost the property storage of 13
toolbar objects and 67 wrappers. Qt's own check (`QV4_MM_CROSS_VALIDATE_INCREMENTAL_GC=1`)
reports the same counts as missed by the incremental marking. With whole runs, none of 480
starts warned.

A whole run took 0.3–7 ms on the desktop with the sample notebook (logging category
`qt.qml.gc.allocatorStats`). `QV4_GC_TIMELIMIT=5` brings back Qt's default, for a comparison.
Whole runs also start more often in the middle of a long function. That is why the self-tests'
runner holds deferred deletions while a pass runs ("The playground's self-tests" below).

### Quitting: the windows before the engine

`main.cpp` deletes the engine's windows right after `exec()` returns, while the engine, the
controller and the application object still exist (`ui::releaseQmlWindows`,
`src/app/ui/qmlshutdown.h`), and delivers the deferred deletions that this starts, round after
round. A guard declared right after the application object deletes whatever QML items are still
its children when everything else is gone (`ui::deleteQmlLeftovers`).

Why: once the app is quitting (`aboutToQuit` has been emitted), a `ListView` or `GridView` that
gives up a delegate makes it a child of the application object and deletes it later. It does so
for a delegate it releases and for one it cancels while it is still being created
(`QQmlDelegateModelItem::destroyObjectLater`, QTBUG-87228). No event loop runs after `exec()`, so
the application object's own destructor deleted those delegates, after
`QCoreApplication::instance()` had become null. A delegate that holds a view of its own, whose
next delegate was still being created, cancels that creation then, and Qt reads the null instance.
That is a journal day built ahead: the journal's `ListView` (a kept view, built asynchronously),
a day's `ContentJournalDay`, its `BlockEditor`'s rows. The AppImage job's QML smoke test crashed
this way once (2026-11-20, SIGSEGV in `destroyObjectLater` from `QQmlDelegateModel::cancel` in the
inner view's destructor): it quits 1.5 s after the start, while the views are being built ahead.
`tst_ui_qmlshutdown` quits that shape at every step of its incubation, advanced by hand. Without
the release Qt crashes at some steps; with it no step crashes and no item is left.

## Dev tools, screenshots and UI tests

Command-line options of `brulion` (`src/app/ui/devtools.h`, `src/app/shell/appcontroller.h`,
`src/app/main.cpp`); unknown options are ignored. Scripts and AI agents that read or change notes
use the separate command line `brulion-cli` instead (docs/CLI.md: JSON output, an MCP server). Headless runs:
`QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software`. The developer QML behind `--gallery`,
`--editor` and `--editor-script` (`qml/dev/`, `qml/content/ContentJournalsSelfTest.qml`) is built
unless the CMake option `BRULION_DEV_QML` (`AUTO`, `ON`, `OFF`) turns it off. `AUTO`, the
default, leaves it out of the shipped Android and browser builds (Release / MinSizeRel, about
0.7 MB less) and is decided again on every configure, so a build dir reused for another
`CMAKE_BUILD_TYPE` follows it. Without it `--editor-script` does nothing (PageView and JournalsView
load the scripts and self-tests from their files only when asked, and find none) and `--gallery` /
`--editor` end at once (exit code 1); the QML tests that need it
(`tst_editor_qml`, `tst_content_journals_qml`) are left out.

| Option | Effect |
|---|---|
| `--gallery [--section <id>]` | the component gallery (`qml/dev/Gallery.qml`) instead of the app; popups of the section open by themselves |
| `--editor [--editor-script <state>]` | the block editor playground (`qml/dev/EditorPlayground.qml`, in-memory `FakeEditorHost`) in a scripted state: `editing`, `suggest`, `tags`, `tag-pages` (the `#` popup offering pages), `slash`, `date`, `date-dialog`, `date-dialog-reminder`, `date-dialog-alarm` (the date dialog of an undated task as a date, a reminder, an alarm), `zoom`, `source`, `reminder`, `table`, `calc`, `query`, `dataview` (a Dataview TABLE of the sample's page, a TASK query, a CALENDAR of the daily notes and inline queries), `mermaid` (a drawn flowchart and an unsupported type), `mermaid-editing`, `chart` (a bar chart of the sample's table and a pie of its calc block), `chart-filter` (the table's filter row with `> 40` typed), …; the document mode on a sample trip plan: `document`, `document-paragraph`, `document-list`, `document-heading`, `document-task`, `document-outline`; self-tests that exit: `selftest` (ctest `tst_editor_qml`; also Dataview's inline queries and a CALENDAR — dots per page, a day's page opened by a tap, the menu of a day with two, the next month —, names with markup in menus and breadcrumbs, a block put at the start of a page by a real Enter or Alt+Up, undone with real Ctrl+Z and redone with Ctrl+Shift+Z, Enter at the end of a zoomed paragraph ending the zoom with the new block edited and in view, Ctrl+Z zooming back in, Ctrl+Shift+Z out again, the breadcrumb's arrow clicked while a child of a folded zoom root is edited), `blocklinks` — links to blocks and headings with real input: the block menu from right-clicks on a heading's text, a paragraph, a callout, a calc block and a page title drawn above the page's properties (none from the properties), a bullet's right-click / long press and the touch toolbar's "More" (above the bar; a table cell's too), a finger's long press on text selecting the block (no menu), each "Copy link" item read back from the clipboard with its toast, Ctrl+Shift+C editing / selected / nothing selected / in a table cell, the palette after a cell lost the keyboard, copied links followed (scrolled, selected, flashed — again after scrolling away), a missing heading (ctest `tst_editor_qml_blocklinks`, `_touch`); states `copylink`, `copylink-document` (the menu with its submenu open on a heading), `copylink-toolbar`, `copylink-table` (with `--touch`: the menu from the toolbar's "More" of a block, of a table cell), `clock` — the round clock with real mouse / touch input (numbers that never overlap and stay legible, down to the smallest dial, with larger text too), its dialog, the clock buttons of the date picker and the calendar's add dialog, whose typed time applies on Enter or when the field is left, never per keystroke (text that is no time keeps Add disabled, its hint shown once it is applied, never while it is typed; today: the next full hour, the past quick times disabled, a past time said) (ctest `tst_editor_qml_clock`, `_phone`, `_landscape`), `taskstatus` — the task status menu with real right-clicks / long presses on the editor's, a query result's and an embed's checkbox (the result's and the embed's menu staying open while their note changes elsewhere, the status written on the task's line now), Ctrl+Shift+Enter and the menu's keys (none on a heading), the "/" status commands typed with real keys, the touch toolbar's task button held down (ctest `tst_editor_qml_taskstatus`, `_touch`), `taskclock` — the task menu's timer (Start timer / Stop timer: the `:LOGBOOK:` CLOCK line written and closed, the "⏱ from 10:00" chip, undo) and "Add dependency…" (the picker, a search without diacritics, 🆔 / ⛔ written, the ⛔ chip showing the task waited for) with real right-clicks / long presses (ctest `tst_editor_qml_taskclock`, `_touch`), `selectiontest` — selecting and copying blocks with real input (`qml/dev/SelectionSelfTest.qml`): Esc and Shift+↓ then Ctrl+C, Shift+↑ / Shift+↓ from an edited block, Ctrl+A twice, Shift+click, Delete and Ctrl+Z, the toast's Undo (clicked; one whose deletion was undone already undoes nothing), Alt+↓ and Tab on a range, Ctrl+V over a selection, in-block Ctrl+C / Ctrl+X keeping no-break spaces, the edited text's right-click menu (its Copy; ↓ highlighting an item and Esc giving the keys back to the text, still selected; its "Highlight…" — Red highlighting the selection as `<mark style="background: #FF5582A6;">`, the text still selected, one undo step), the block menu of a selected and another block, the floating tools, a mouse drag over a document's paragraphs (no scrolling) and to the view's edge (autoscroll), the band after scrolling away, the floating tools' More (no indents for a document's paragraphs); with `--touch` a long press, taps, the end grip, Delete and a tap on the toast's Undo, a long press dragged to the edge, Back, the toolbar's and the bullet menu's "Select" — the clipboard read back (ctest `tst_editor_qml_selection`, `_touch`), `tabletest` — a table's rows and columns with real input (`qml/dev/TableSelfTest.qml`): the cell's Row and Column from a right-click, also on the cell being edited, and the toolbar's More (none in a block menu not from a cell), a new cell edited with the keys once the menu is gone, Alt+arrows and Ctrl+Enter in a cell (not the app's Back and Forward), the header's disabled items, the alignment's tick, deleting, formulas following, `REF!`, sorting from the header (the summary row staying, the rows' lines kept, rows already in order writing nothing), the filter row (`> 40` typed hides a row, nothing written, "Clear filters"), "Copy as CSV" read back from the clipboard, a ```` ```chart ```` of the table following a changed cell, with a keyboard (desktop) the arrows, Tab and Home / End between cells and Shift+arrows / Shift+click, Ctrl+C / Ctrl+X / Delete / Ctrl+V and a typed character on a rectangle of cells, the block menu's "Copy cells as Markdown", rows added by a paste, undo back to the same bytes (ctest `tst_editor_qml_table`, `_touch`), states `date-menu`, `date-menu-reminder` (a 📅 / ⏰ chip's menu), `recurrence-menu` (a 🔁 rule's), `link-menu` (a #tag's), `datechips` — the token menus with real input (`qml/dev/DateChipSelfTest.qml`): a right-click / long press on a 📅 and a ⏰ chip (no block menu, nothing selected or toggled), A day later, Today, Change date… (the dialog on the chip's date, a date and time written; switched to Reminder, the 📅 becomes a ⏰ in its place), Remove date, An hour later, Alarm, the reminder dialog, each undone; a plain click / tap on the 📅 chip opening the date dialog on it (no editing, no menu), its "+" choosing Reminder, a second due date refused (the hint, no "Add"), the reminder written right after the 📅 and undone in one step; the ⏰ of an edited block still a chip, a click / tap on it opening the reminder dialog with the caret kept, the time chosen written and undone; an undated task's "Add date" button (shown on hover and while edited, never on a done task; a dated task's is its "+": the date dialog adding a reminder at the end of its line, the date kept, undone), its menu's Today / Tomorrow / Next week dates, Tomorrow written as 📅 with the block still edited, "Pick a date…" on tomorrow with nothing to remove, then switched to Alarm (a tap / click on the choice: a ⏰! with a time, no 📅), each undone; a priority set lower, a 🔁 rule set to Daily, the reminder's Priority submenu, a `[[day]]` link a day later (each undone), the link menus of a `#tag` and a `[[link]]` (Copy read back from the clipboard) (ctest `tst_editor_qml_datechips`, `_touch`), `linkpartstest` — the path parts of `[[Firma/Adam cośtam]]` with real input (`qml/dev/LinkPartsSelfTest.qml`): the mouse over a part underlining it alone, over the separator nothing; clicks on a part (its route), the page and the separator (editing, the raw line unchanged); a display-text link staying one; the menus of a folder's part (Show folder, Copy folder path read back) and a page's part (Copy link `[[Firma]]`); parts in a table cell, a property, an embed and a query result; Alt+Enter (a part, the page, Alt+Shift+Enter, nothing outside a link, a table cell's field); a simulated screen reader's link items (named, over their links, the press action, the focus ring); with `--touch` the press underline and a flick opening nothing (ctest `tst_editor_qml_linkparts`, `_touch`), states `linkparts` (a part hovered: underlined) and `linkparts-menu` (a folder part's menu), `readingtest` — reading mode with real input (`qml/dev/ReadingSelfTest.qml`; "Reading mode"): a click / tap on text editing nothing with the toast and its "Edit", a checkbox changing nothing, a link opening, the block menu without Cut / Move / Delete, Delete and Enter on a selected block, "Edit" leaving the mode, the mode turned on while editing (no refusal toast), the file as written (ctest `tst_editor_qml_reading`, `_touch`), `propstest` — the page properties panel with real input (`qml/dev/PropertiesSelfTest.qml`; "Page properties"; the key suggestions following their field when it moves) (ctest `tst_editor_qml_properties`, `_touch`), states `properties`, `properties-phone` (the panel expanded on a page with every type), `refreshtest` — views following what they depend on (`qml/dev/RefreshSelfTest.qml`): a query's results replaced by as many rows of another height, an embedded section changed in its own note, a date picker's today marker after midnight, the reminder dialog's weekday names, rule and date picker after a language change, a calculation's results after the window narrowed until the text wraps again (the width depends on the fonts), the view staying down the page when the page's header changes its height (ctest `tst_editor_qml_refresh`, `_touch`); states `selection` (a range; desktop: the floating tools, `--touch`: the docked tools and grips), `selection-document`, `selection-menu` (the block menu of a selected block), `text-menu` (the edited text's menu), `keyboard` — the touch toolbar docked on a simulated keyboard (with `--touch --keyboard 0`; ctest `tst_editor_qml_keyboard_touch`) |
| `--profile <name>` | a separate profile for this run (as `BRULION_PROFILE`, src/ipc/profile.h): settings, app data, recent notebooks and sockets of "Brulion-<name>"; `--selftest-*`, `--journey` and `--screenshot` without a home of their own (no `XDG_CONFIG_HOME` other than `~/.config`, no `BRULION_PROFILE`) use "test" by themselves, and say so on stderr. `brulion-cli --profile <name>` reaches that run |
| `--demo` / `--vault <path>` / `--onboarding` | seed and open the sample notebook of the UI language (in `--vault`, else "Sample notebook", with the Polish UI "Przykładowy brulion") / open this folder / start on onboarding (without the intro) |
| `--sample-language pl\|en` | the language of the sample and documentation notebooks for this run (default: the UI language, `--locale` included) — e.g. the English UI with the Polish sample notes a self-test is written against (`tst_shell_qml_capture`) |
| `--intro` | start on the intro (`IntroView`), also on a device that finished it |
| `--route <view or JSON>` | initial route, e.g. `settings` or `'{"view":"page","path":"Projekty/Remont mieszkania.md","open":"links"}'` |
| `--share <json>` | receive a share as Android's `ShareActivity` hands it over — `{"subject", "text", "url", "files": [{"path", "name", "mime"}]}` (`src/core/notebook/share.h`) — and open the share dialog once the notebook is ready (`AppController::receiveShare`; screenshots, manual checks, ctest `tst_shell_share`) |
| `--share-query <query>` | the browser version's share target: `share-title=…&share-text=…&share-url=…` as `wasm/index.html` passes a share launch's parameters (`notebook::sharedContentFromQuery`, `AppController::receiveShareQuery`); works on every platform (ctest `tst_shell_share`) |
| `--uri <address>` | a brulion:// address to receive once the notebook is ready (also a bare `brulion://…` argument, and one among the `--open` files: the desktop entry's `%U`); a second launch hands it to the running Brulion (`uri`, ipc/appmessage.h) — "Automation" above |
| `--open <file>…` | the files to offer in the share dialog ("Open with": the desktop entry's `Exec=brulion --open %U`, the Windows installer's `"…\brulion.exe" --open "%1"`): every argument after it is a file — paths relative to the working folder or `file://` URLs —, never an option, the options are the arguments before it (`notebook::splitOpenArguments`); a file of the open notebook opens as its page; copies are saved, the files are never changed; without files a plain start. A second launch sends them to the running Brulion (`share`, ipc/appmessage.h) |
| `--update-available <version>` | show the message about a new version as if `<version>` were the latest release, on any platform, without asking GitHub; for this run only (the `update` screenshot) |
| `--popup <name[:query]>` | open a shell popup once the notebook is open and indexed (at most 3 s later without one): `palette`, `switcher` (the query as if typed, the caret after it), `capture`, `newPage`, `newFolder`, `move[:<path>[><folder>]]` (else the page shown; with `>` that folder chosen), `about`, `drawer`, `more` (the top bar's "More" menu) |
| `--editor-script <state>` (with the app) | `qml/dev/PageScript.qml`: the editor on the guide page (a document) in a state (`editing`, `slash`, `reminder`, `source`, `sidepanel`, `doc-paragraph`, `doc-list`, `doc-heading`, `doc-outline`; any page given by `--route`: `edit:<text>[@<caret>]`, `select:<text>`, `select-range:<first>..<last>` — on touch UIs in the selection mode —, `mode-tooltip`, `properties` — the page's properties panel expanded, nothing focused, the view at the top: the `ui` benchmark's screen —, …) |
| `--screenshot <file> [--delay <ms>]` | save the window after loading and quit; with `--popup` also once that popup has opened — its enter transition over, so a shot never catches it half faded in (`DevTools.holdScreenshot`; at most 5 s longer, then with a warning) |
| `--dump-layout <file>` | with `--screenshot`: write what the window shows (texts with colours and fonts, targets, boxes, theme tokens) as JSON for the UI benchmark (`src/app/ui/layoutdump.h`), from the same frame |
| `--size <WxH>`, `--scale <f>`, `--touch`, `--theme light\|dark`, `--locale pl\|en`, `--reduced-motion`, `--now "<date> <time>"` | window size, device pixel ratio, touch UI, theme, language, motion, fixed clock |
| `--accent <name\|#rrggbb>`, `--surface paper\|white\|ink\|slate\|black`, `--density compact\|comfortable` | the appearance settings for this run (screenshots; the settings themselves stay): accent preset or custom colour, light or dark background, density |
| `--keyboard <px>` | a simulated on-screen keyboard of that height (`KeyboardInsets.simulated`; 0 = none, and a real one is ignored), drawn as a grey block of keys |
| `--journey <dir>` (`--journey-theme`, `--journey-empty`) | walk the real app like a first-time user with real input events, a screenshot per step (`src/app/shell/shelljourney.h`; desktop builds) |
| `--selftest-shell` | key routing and focus checks of the real shell, the command palette's and quick switcher's rows (one line high while floating, a subtitle beside the title; two lines on a phone), and rows with context menus with real input — taps and long presses (`--touch`: the drawer, All pages, tasks, bullets — a bullet's tap zooms in and Back zooms out a level at a time before it leaves the page; a nested block of today's journal at home zoomed out one level per Back before Back is the system's again) or left and right clicks, and on the desktop the keys (a tree row's menu from the Menu key: ↓ highlights its first item, Esc gives the row the focus back, ↓ ↓ Enter on "Open in side panel" opens the page there; the block menu walked with ↓ over the items shown to "Zoom in", Enter zooming in); the drawer's edge swipe (`--touch`: a finger from the left edge opens it, to the left closes it, with a simulated gesture-navigation zone (`GestureInsets.simulate`) from beyond it; a vertical scroll and a swipe farther in leave it closed, a tap in the strip reaches the button under it, no strip while blocks are selected or the keyboard is up, so the selection's start grip at the left edge keeps its drag — the drawer opened by its button still closes with a swipe —, nor on the calendar's week and month grids, whose swipe from the left edge — not a fast flick — shows the previous period, and the drawer opened over them closes with a swipe); the task status menu on the checkboxes of Tasks (and its Menu key: the status highlighted, Up to "Open", Enter), a page's linked references, the calendar's agenda (a day's header selecting the day — the panel or a phone's sheet following, the list staying where it is; Down and Enter on a desktop), its day panel (on a phone a bottom sheet, left open) and a journal day, and a "/" status command typed into the journal; links to blocks and headings (a link to a block moved to another page opens that page with the block selected, a missing heading is said — from the text and as a route; a heading named with tags quoted as plain text —, the palette's "Copy block reference" copies the edited block's `((id))`, read back from the clipboard, a right-click on a paragraph opens the block menu, the side panel's outline copies a heading's link — the last two on the desktop); tags as page links (a tap / click on a `#tag` in the journal opens its page — not created by looking at it — with the tagged blocks among its references and the tag overview's "#" in its header — the page menu's "Tasks with this tag" opening Tasks filtered by the tag, no row of buttons under the title, a tag's sub-tags as chips there —, back returns; in Tasks a click filters, Ctrl+click opens the tag's page, a long press its menu — "Filter by tag", its "Open tag page" opens the page; a row of the tag list opens the page, its button the overview); in Tasks a due date's chip has the date menu, whose "A day later" writes the task's note, and a tap / click on it the date dialog with "+", whose "Add" writes a reminder right after the date (only that), and an undated task's "Add date" chip a quick date menu, whose "Tomorrow" writes the 📅 on the task's line only; a picture attachment (its view, its bar on a phone and in a narrow view beside the side panel, no live tooltip outliving its hidden button, the text view read-only and scrolled by a finger or the wheel, its byte limit cutting before a character, a change on disk shown at once, Ctrl+E both ways, its references, a small picture, a name with markup, a format without a decoder, the top bar's More menu there, the same picture embedded in a note); PDFs ("PDF viewer": a note's thumbnail drawn on a white page, a tap / click opens the viewer without loading the PDF into the editor, "Page 1 of 3", Ctrl+G and "3" typed with real keys over the page shown, selected — "7" said past the PDF's 3 pages, the subtitle "3 pages" — then Enter, Home, PageDown, Ctrl++ zooming with sideways panning, Ctrl+0, a pinch — `--touch` — or Ctrl+wheel, a double tap both ways, Ctrl+F "gesla" finding "gęślą" with a highlight, a PDF without text saying so — on the phone run on the line under the search row, ‹ › hidden, × on the screen —, Esc closing the search and then the viewer, the note's link with `#page=3`, "Draw on this page" on page 2 — the drawing's backdrop is page 2, embedded right after the PDF's block, Back returns to the viewer on page 2, the second time the drawing is offered and opened, no second file —, More → "Open in another app" through a recording hook, a locked PDF from `tests/data/pdf` — `BRULION_SELFTEST_PDF_DIR` —, a ZIP read-only; a build without a renderer: the chip, its click hands the file over, the viewer's message); a drawing (a tap / click on its embed opens the drawing editor, a stroke drawn with a finger — `--touch` — or the mouse is saved by itself with its undo kept, Undo / Ctrl+Z is saved too, the page's embed shows the drawing saved again; a text being written low on the paper stays on it when the window shrinks — a browser's keyboard); the Markdown source scrolled by a finger, a tap and a long press in it (`--touch`); the default reminder time set on the clock in the settings; a page's tasks ignored from its menu (`zadania: ignoruj` written, the task out of `type:tasks`) and shown again; the AI assistant's ✨ on the top bar (none while the assistant is off, nor the palette's AI chat; its menu on a page — "Summarise this page" enabled, the conversations —, "New conversation" opening the AI chat about the page — its chip, the field, the suggestions, the field above a keyboard over half the window, nothing to send yet, no conversation started —, Back to the page, "Conversations" opening the list, "AI settings…" opening Settings → Extensions); the settings' sections as tabs on a phone (the route's one checked and in view, the bar scrolling sideways, a tap showing another section without a history entry, a route from elsewhere moving the tabs along); the event log page (a logged line arrives without its secret, the search ignores diacritics, an area chip filters, the detailed switch) and a sync run's details from the history ("Show in the event log"); the sync center's conflicts (a copy reached by the overview's "Merge": on a phone opened through the route, Back returning to the list, "Keep mine" too; on a desktop the list beside it); folders (the page opened revealed and highlighted in the tree, a row's "⋯" (`--touch`) or right-click menu, "Move to…" moving a page with the dialog, a root folder from the "Pages" header's "+" typed with real keys (a folder that exists, typed in another case, said at the field first, Create disabled), an empty folder deleted leaving the tree, with the mouse a page dragged onto a folder and onto the header, a closed folder opening under a held drag and Escape cancelling it, All pages' folder rows and the breadcrumb back); Settings → Appearance (the logo's colours by default, a tap on an accent swatch recolours the app and is saved, a colour typed in the accent dialog, a sidebar entry unticked leaves the sidebar — the drawer on phones — and stays in the palette, a bottom navigation slot takes a view from its menu and the phone's bar shows it — the view it already has, picked again, stays checked —, the quick note button switched off and on, "Restore the default look…" at the Appearance card's end, asked first and confirmed; Editor → Font → "Other…" cancelled leaves the selection where it was and opens the picker again, a missing family names the serif or sans-serif font used instead); the share dialog ("Share → Brulion": shared text saved to today's journal by a real tap / click on "Save" without a reminder, a shared `.md` saved as a new page titled after it byte for byte and its copy removed, a file opened with Brulion from outside the notebook — the "to save in the notebook" subtitle and the copy hint, saved as a page byte for byte, the file itself left unchanged —, Escape dropping a share); tabs ("Tabs, split view and windows": Ctrl+T, Ctrl+Tab, Ctrl+Shift+Tab, Ctrl+1 / Ctrl+9 and Ctrl+W with real keys, each tab's own history, a middle click and a Ctrl+click on a sidebar page, a middle click on a link and the link menu's "Open in new tab", a tab dragged in the bar, a tab back where its page was scrolled, no editor for a tab not shown, the split view — a click focuses a pane, Ctrl+W closes it —, a page window seeing the main window's edit, the event log's tab titled "Event log" with the log's icon; `--touch`: no bar, the top bar's tab switcher with the shown tab checked, the back key in a tab); find over the journal stream's days (the count over every day with "· 2 days", Enter going on to another day with a match — scrolled to and written in, the keyboard kept in the field —, Shift+Enter back, nothing written); weekly and monthly notes (this week's note opened by `AppController.openPeriodicNote` without writing it, its period under the title, the next week's arrow, a week number of the calendar's month grid and the month's title opening those notes); a page's icon and cover (the page menu's "Add icon…", an emoji picked in the icon dialog written as `icon` — that line only — and shown before the title and, on a desktop, in the sidebar's tree; "Add cover…" taking a picture of the notebook, written as `banner` and shown as the band at the top; both removed from their dialogs, the file as it was); a page named with markup (`<b>Ważne <i>notatki.md`) quoted as written — plain text — by the Move dialog's title and, in its frame, the trash confirmation; the tree's daily notes relabelled in the language switched to at run time, and back (desktop); Vim keys ("Vim mode": the setting on, real keys typed into a page — `w`, `cw` with Polish letters, `dd`, `u`, a visual line selection over two blocks, `:%s` — and into its source mode, the mode shown in the status bar or the view's corner, typing again with the setting off); the views a route left deleted with their editors — rounds of pages and views leave no more `PageController`s than the first, give or take a journal day loaded later (`PageController::liveCount()`), once the self-test has run the deferred deletions its nested event processing holds back (ctest `tst_shell_qml`, `tst_shell_qml_capture`; desktop builds) |
| `--selftest-onboarding` (with `--onboarding`) | the start screen ("The start screen" above): recent notebooks — a missing one marked, clicked (a warning, nothing created) and removed from the list, a vanished temporary one dropped, "Show all" on phones —, the layout (two columns without scrolling at 1280×800, one column on a phone), the one primary button creating and opening a typed folder, back on the start screen first with the keyboard on it (Down, Delete) (`src/app/shell/onboardingselftest.h`, ctest `tst_shell_onboarding_qml*`; desktop builds) |
| `--selftest-import` (with `--onboarding`) | the ways to the import screen (onboarding, empty journal, drawer, More) and an import from a typed path, the exports' save dialogs, printing a page and publishing a page (a new folder target, the dry run, "Publish"), with real taps / clicks (`src/app/io/ioselftest.h`, ctest `tst_appio_import_qml*`; desktop builds) |
| `--selftest-sounds` (with `--demo`) | Settings → Reminders: the sounds of reminders and alarms (Silent, Ringtone without one → the sound menu, a theme sound, the ringtone back, Play sample), with real taps / clicks (`src/app/notify/notifyselftest.h`, ctest `tst_notify_sounds_qml*`; desktop builds) |
| `--scroll-probe <target>` (`--scroll-script drag\|flick\|wheel\|touchpad\|observe`, `--probe-out <csv>`, `--probe-passes`, `--probe-wait`, `--probe-max`, `--probe-speed`, `--probe-check <px>`, `--probe-row-bar`, `--probe-cache <px>`) | scroll a view with synthetic input (touch drags / flings, mouse wheel, touchpad) to the end and back — or, with `observe`, record real input (a finger, `adb shell input swipe`) for `--probe-max` ms — record every frame (interval, GUI work, contentY / originY / contentHeight, delegates created (once the view has placed them) — with their kind of block — and resized above / in / below the view, the content's movement on screen against the input, touch moves per frame) and print a summary of stutters, jumps and touch strokes (release speed, fling, its start velocity from its distance; TouchFlick's corrections and caught flings), then quit (`--probe-check <px>`: exit code 1 when the content jumps by more than <px> — at an end of the list only layout shifts count — or the thumb of the view's `BRowScrollBar` jumps (against the content, or while it stands still) — with `--probe-row-bar` also when the view has no such bar over it —; a `SHIFT` line per layout shift (up to four) with the anchor's row, the frame and the delegates `row@y+height` before and after it, ctest `tst_scroll_jumps_qml`; `--probe-cache <px>`: the list's cacheBuffer first, 0 = every row created at the edge of the view); the log also goes to `<csv>.log` (and logcat on Android) (`src/app/ui/scrollprobe.h`; target `auto`, `sidebar`, `name:<objectName>`, `type:<QML type>`). Android: Qt passes the activity's `applicationArguments` string extra only to debuggable APKs (`am start -n io.github.januszbit.brulion/.android.BrulionActivity --es applicationArguments "'--scroll-probe auto --scroll-script observe --probe-out /data/user/0/io.github.januszbit.brulion/files/p.csv'"`). Built unless `BRULION_SCROLL_PROBE=OFF`, the default of the shipped Android and browser builds (Release / MinSizeRel): measure with `BRULION_ANDROID_BUILD_TYPE=RelWithDebInfo` |
| `--hidden`, `--capture`, `--today`, `--no-single-instance` | start in the tray (Windows' start at sign-in; a second launch with it shows nothing) / open quick capture / today's journal (a second launch asks the running instance — over a socket only in the user's private runtime folder: without one every launch is an instance of its own and the event log says why, docs/CLI.md "The running app") / allow a second instance |
| `cli …` (the first argument) | the command line instead of the app (`brulion cli capture "…"`, also `Brulion.AppImage cli …`): `brulion-cli`, before anything of the app — no window, log file or single-instance claim; see docs/CLI.md. A running app also takes `brulion-cli app open <ref>` / `app today` / `app capture` (the `open {json}` message of `src/ipc/appmessage.h`, `AppController::openRoute`) |
| `--remote-control` | AI agents and scripts drive the running app for this run (as the device setting Settings → Application → "For developers" → "Allow agents to control the app (local socket)" does; off by default): a request/response socket of its own next to the single-instance one, owner-only — `brulion-cli app state \| navigate \| open \| find \| click \| long-press \| double-click \| drag \| scroll \| type \| key \| command \| capture \| wait \| screenshot \| quit` and the MCP tools of `brulion-cli mcp --app-control` (docs/CLI.md "Remote control"; `src/app/shell/remotecontrol.h`, desktop builds). Input is played on the event loop with the UI driver of the self-test and the journey (`src/app/shell/uidriver.h`), one gesture at a time, answered once the window settled; ctest `tst_remote_control_qml` drives the real app offscreen through it |

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
  picture view with "Edit drawing" (`picture`), an Excalidraw drawing with text (`readonly`),
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

### The playground's self-tests (`qml/dev`)

The playground's self-tests (`--editor-script selftest`, `clock`, … and the files `qml/dev/*SelfTest.qml`)
run on one step runner (EditorPlayground: `runSteps()`, `stepTimer`), compiled to C++ like the rest
of the QML ("Compiled QML"): a test's steps are a function that writes each step as
`if (window.at()) { … }`, in order; each tick of the runner calls it once and `at()` is true for the
one step to run then (`stepIndex`). A test longer than about 20 steps writes them in parts — the
function calls `stepsPart1()`, `stepsPart2()` … in order, each with at most about 20 steps
("Compiled QML", "Short functions": qmlcachegen's time over one function grows steeply with its
steps); a new step goes into the part where it belongs, a part that grew past that is split. A
helper standing for several steps calls `at()` for each of
them; steps for one kind of device only sit in an `if (Theme.touchUi)` (a condition that holds for
the whole run). A step waiting for something asynchronous repeats itself (at most `waitLimit`
times): `if (window.at()) window.waitStep(ready, "what")`, or `window.waiting(ready)` alone for a
finger held down until its gesture took effect. A step that throws is a failure and the next one
runs (`DevTest.callCatching()`). Real input goes through `DevEvents` (QtTest's TestEvent, made when
first used: `mouseClick()`, `touchPress()`, …); `DevBlock` gives a block's fields, `DevRecord` and
`DevTest.valueAt()` any map's, `DevTest` finds items by name (`src/app/ui/devtesting.h`).

While a pass runs, `callCatching()` holds the deferred deletions (`deleteLater()`) that come due
and deletes those objects once the pass returned. QtTest's events process events inside the pass,
and compiled QML keeps the objects a function used as plain pointers that the garbage collector
reads. An editor's field that Escape closed was deleted by a touch's events, and a collection later
in the same pass (a menu being made) read the freed object and crashed the app
(`tst_editor_qml_properties_touch`, 5 % of runs under load once collections did whole runs).
The hold covers `deleteLater()` only, so C++ that removes an item QML can reach deletes it later
too: `JournalDayItem` takes the text items of rows it no longer draws out of the scene and calls
`deleteLater()` (`textItemOf()` hands them to the span self-test; deleted at once by the tap that
made the day editable, they crashed `tst_content_journals_span_qml_touch_dayitem` in every run of
the Nix build — `QV4_MM_AGGRESSIVE_GC=1`, a collection at every allocation, shows such a crash in
any build).

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
| Themes | `--theme light\|dark` | the setting "Base color scheme" set to Light / Dark (`app.vault.setConfig('theme')` + `app.updateTheme`; a "Use light/dark mode" command when the version has one, never the toggle) |
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

**Outcome (October 2026, branch `s2/ui`).** The loop ran its 8 rounds and met the win condition
in round 8 and in the confirmation (all 24 groups re-judged by 3 fresh judges, new seed and A/B
draws, on round 8's shots):

| Round | P Brulion | P Obsidian | Margin (95 % CI) | J B / O | O B / O | Pairs won | Kept |
|---|---:|---:|---|---|---|---:|---|
| 0 (baseline) | 7.472 | 6.926 | 0.546 (0.368–0.755) | 7.065 / 6.508 | 8.691 / 8.179 | 30/48 | — |
| 1 | 7.581 | 6.845 | 0.736 | 7.148 / 6.401 | 8.876 / 8.179 | 38/48 | yes |
| 2 | 7.669 | 6.824 | 0.845 | 7.268 / 6.372 | 8.870 / 8.179 | 43/48 | yes |
| 3 | 7.681 | 6.816 | 0.865 | 7.286 / 6.362 | 8.865 / 8.179 | 42/48 | no: +0.012 and O lower, reverted |
| 4 | 7.635 | 6.830 | 0.805 | 7.221 / 6.380 | 8.875 / 8.179 | 40/48 | yes (O rose, J −0.047) |
| 5 | 7.748 | 6.828 | 0.920 | 7.341 / 6.378 | 8.967 / 8.179 | 41/48 | yes |
| 6 | 7.828 | 6.793 | 1.035 | 7.448 / 6.331 | 8.967 / 8.179 | 43/48 | yes |
| 7 | 7.881 | 6.760 | 1.121 | 7.521 / 6.286 | 8.961 / 8.179 | 46/48 | yes |
| 8 | 7.875 | 6.730 | 1.145 (1.013–1.307) | 7.513 / 6.247 | 8.962 / 8.179 | 48/48 | yes, without its change 4 |
| confirmation | **7.997** | **6.808** | **1.189 (0.966–1.335)** | 7.674 / 6.352 | 8.962 / 8.179 | **48/48** | — |

In the confirmation every criterion favours Brulion (hierarchy 7.81 vs 6.31, typography 7.60 vs
6.54, spacing 7.41 vs 6.23, colour 7.68 vs 6.63, consistency 7.60 vs 6.09, calm 7.05 vs 6.81,
affordance 7.97 vs 6.19, appeal 7.83 vs 6.10); calm is the closest, and the narrowest pairs are
calendar-desktop (margins 0.37 / 0.40), settings-phone (0.39 / 0.41) and palette-desktop (0.54 /
0.55). The panel was not blind in practice: the judges recognised Obsidian in 142 of 144 guesses
(every round about the same), so J measures a preference between two known apps, not a blind
one — O, from the metrics, is the part that cannot know. Rejected changes (round 3's five, round
8's emoji-free date chips: `rejected.md`, `round-08/change4-dropped.patch`) and every round's
plan, shots, votes and report stay in `/home/nixos/workspace/brulion-work/uibench/`; to check for
a regression, run the bench again (`--app brulion --reuse-obsidian` on the confirmation round's
Obsidian shots) and compare with the confirmation's row above.

**Regression guard** `tst_uibench_lint_qml` (`lint.sh`, slow): 8 Brulion shots (daily, doc,
settings, palette × desktop light / phone dark; system fonts) fail on any enabled, visible text
below its WCAG contrast or a target below its size that `lint-allow.json` does not excuse
(`[{screen, device, kind, type, text|name, count, reason}]`: `count` excuses that many failures of
a shot of that device, so one more is caught). Its entries were the round-0 baseline of Brulion's
known failures (the bullet's zoom handle, the journal's day titles, its 12-px band under a day and
the settings' 38-px segmented choices on phones, the calculation block's faint caption); round 1
fixed them but one: a parent's bullet beside its fold arrow on phones (28 × 44 — both cannot be
44 px wide in the outline's gutter), accepted as an exception at the loop's end. It reads
`screens.json` with node and is skipped where there is none (the AppImage build containers;
GitHub's Ubuntu runners of `ci.yml` have it, the flake's dev shell does not bring it).

**README pictures** (`docs/images/`, English locale, the English sample notebook) come from these
scripts — `LOCALE=en SIZES=<size> THEMES=<theme> tests/app/<area>/screenshots.sh <shot>` (the
files get a `-en` suffix; the scripts' default locale is still Polish) — and are optimised with
`pngquant --quality 70-90 --strip` then `oxipng -o 4` (`nix shell nixpkgs#pngquant
nixpkgs#oxipng`; keep each under 150 KB):

| Image | Area, shot | Size, theme |
|---|---|---|
| `journal.png` | content `journals` | desktop, light |
| `page-links.png` | content `side-links` | desktop, light |
| `calendar.png` | planning `calendar` | desktop, light |
| `tasks-dark.png` | planning `tasks` | desktop, dark |
| `reminder.png` | shell `page-reminder` | desktop, light |
| `graph-dark.png` | planning `graph` | desktop, dark |
| `sync.png` | io `sync-conflict-merge`, run without the Nextcloud account of `io_nextcloud_home` (so the sidebar footer shows no connection error) | desktop, light |
| `phone-journal.png` | content `journals` | phone, light |
| `phone-editing-dark.png` | shell `page-editing-keyboard` | phone, dark |
| `folders-move.png` | shell `move` | desktop, light |
| `phone-selection.png` | shell `page-selection` | phone, light |

The phone pictures are also the store screenshots (`fastlane/metadata/android/en-US/images/
phoneScreenshots/`, docs/ANDROID.md); the Polish store listing has its own Polish ones in
`fastlane/metadata/android/pl/images/phoneScreenshots/` (the same shots run with `LOCALE=pl`).
