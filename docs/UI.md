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
    favourite, rename, history, trash); a sync status footer (opens the sync center).
  * Top bar (`ShellTopBar`): sidebar toggle, back / forward, the title (a breadcrumb for pages
    in folders), Szukaj, Paleta poleceń, Szybka notatka, side panel toggle and **Więcej** (⋮).
  * Side panel (`ShellSidePanel`): a page opened "in the side panel", or the current view's
    context — for pages and journals the tabs **Linki** (linked references and unlinked
    mentions), **Konspekt** (headings), **Kalendarz** (month with journal days).
* **Narrow (< 900 px, phones):** the sidebar becomes a `Drawer` (swipe from the left edge or
  the ☰ button); the top bar keeps ☰, back (when there is history), title, Szukaj and Więcej;
  a **bottom navigation** bar (Dziennik, Kalendarz, Szukaj, Zadania, Menu → drawer); a floating
  quick-capture button (hidden while editing). The Android back key / gesture walks back
  through the history; the app closes only from the home view.
* **Więcej menu** (top bar ⋮): on a page or daily note — Dodaj do ulubionych / Usuń z
  ulubionych, Zmień nazwę… (F2, pages only), Historia strony, Źródło Markdown (Ctrl+E), Przełącz
  tryb notatki, Otwórz w panelu bocznym (wide only), Przenieś do kosza; always — Nowa strona (Ctrl+N), Paleta
  poleceń (Ctrl+K), **Centrum synchronizacji, Import i eksport, Kosz (n), Ustawienia (Ctrl+,),
  O Brulionie**. These destinations have no button of their own: on phones this menu (and the
  command palette) is the way to them.
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
| `sync` | `SyncView` — Przegląd, Syncthing, Nextcloud, Konflikty (merge) | `tab?`, `copy?` |
| `importExport` | `ImportExportView` | `tab?`, … |
| `history` | `HistoryView` — versions of a page, diff, restore | `path` |
| `trash` | `TrashView` | — |
| `settings` | `SettingsView` — Ogólne, Edytor, Notatnik, Przypomnienia, Synchronizacja, Aplikacja | `section?` |

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
* **Autocomplete popup** while typing (`SuggestionModel`, `SuggestionPopup`): `[[` pages
  (+ create), `((` blocks (creates the target's block id), `#` tags, `/` slash commands,
  natural-language dates after `📅 ⏳ 🛫 ⏰ ⏰!` or `@` (Polish and English, e.g. "jutro o 9";
  `@` also offers the journal link and a reminder).
* Slash commands (`/`, Polish and English names, diacritic-insensitive filter): Zadanie,
  Przypomnienie…, Alarm…, Termin…, Zaplanuj…, Powtarzaj…, Dziś, Jutro, Data…, Tabela,
  Obliczenia, Zapytanie, Nagłówek 1–3, Cytat, Callout, Kod, Szablon…, Obraz…, Osadź,
  Odwołanie do bloku, Priorytet wysoki / niski, Linia pozioma, Aktualna godzina. Under each
  name: the syntax it writes ("⏰ data godzina", "- [ ] zadanie", "blok ```calc").
* A day without a note (journal stream or a single day) starts from the journal template; the
  note is created by the first change (`PageController.startNewPage`), never by looking at it.
* Special block delegates: code (monospace), `calc` (input lines with right-aligned results),
  table (grid editor with formula results; edits rewrite the Markdown table), `query` (live
  results with checkable tasks), callout, embeds/transclusions (`![[…]]`, `!((…))`), images,
  headings, horizontal rule, properties / frontmatter ("Właściwości" panel).
* Chips inside rendered text: 📅 due (red when overdue), ⏳ scheduled, ⏰ reminders (amber,
  bell for alarms), 🔁 recurrence in Polish words. Clicking a ⏰ chip opens the reminder dialog
  (edit / snooze / delete). "dziś"/"jutro" and overdue colours of open pages move on at
  midnight (`DayClock`). Links to daily notes read as dates (`[[2026-10-13]]` → "wt 13 paź").
* Source mode (Ctrl+E, Więcej → Źródło Markdown): one text area with the whole file and
  highlighting.
* Pictures: Ctrl+V pastes a copied picture as an attachment, files dropped on the page are
  stored in the attachments folder and embedded, "/Obraz…" and the toolbar pick files.
* Mobile toolbar (`MobileEditToolbar`, touch UIs, above the keyboard; scrolls sideways):
  Zmniejsz / Zwiększ wcięcie, Przenieś w górę / w dół, Zadanie, Link do strony (`[[`), Tag
  (`#`), Polecenia (`/`), Termin, Przypomnienie, Wstaw obraz, Cofnij, Ponów, Ukryj klawiaturę.

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

## Shared components (`qml/components/`)

`BButton`, `BIconButton`, `BTextField`, `BTextArea`, `BSearchField`, `BSwitch`, `BCheckBox`,
`BChip`, `BBadge`, `BTabBar`/`BTabButton`, `BSegmented`, `BBreadcrumb`, `BCard`, `BListItem`,
`BSectionHeader`, `BKbd`, `BText`, `BDialog` (bottom sheet on narrow windows),
`BConfirmDialog`, `BMenu`/`BMenuItem`/`BMenuSeparator`, `BToast` (+ `BToastHost`),
`BToolTip`, `BEmptyState`, `BProgressBar`, `BSpinner`, `BScrollBar`, `BSplitHandle`,
`BShadow`, `BFocusRing`, `BDatePicker` (month grid, Polish names, natural-language input with
live preview), `BReminderDialog` (date/time, alarm switch, recurrence picker, quick picks: za
10 min, za godzinę, dziś 19:00, jutro 8:00, poniedziałek 9:00), `BDiffView`, `BMergeView`
(per-conflict choice: Moja / Ich / Obie / Ręcznie), `BPalette` with `BCommandPalette` and
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
* **C++ helpers** (QML singletons): `DatesHelper` (natural-language dates, human formatting,
  recurrence rules, localised names, month grids — all values are strings, no JS `Date`),
  `TextHelper` (diacritic folding, fuzzy ranking for palettes, escaping), `DiffHelper` (Myers
  line diff, word-level highlights, conflict hunks), `DeviceInfo`, `DevTools`. The first three
  are QML-free classes in the static library `brulion_ui_helpers` (`src/app/ui/CMakeLists.txt`,
  QtCore + core modules, unit-tested by `tests/app/tst_uihelpers.cpp`); the app registers them
  through `QML_FOREIGN` wrappers in `src/app/ui/qmlforeign.h`. New helper API: add it to the
  class, it reaches QML and qmllint through the library's extracted metatypes.
* **Icons**: Lucide SVGs in `assets/icons/` (see its LICENSE), `BIcon { name: "bell" }`.
  New icons: copy from `lucide-static`, normalise like the others, re-run CMake.

## Dev tools, screenshots and UI tests

Command-line options of `brulion` (`src/app/ui/devtools.h`, `src/app/shell/appcontroller.h`,
`src/app/main.cpp`); unknown options are ignored. Headless runs:
`QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software`.

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
| `--selftest-shell` | key routing and focus checks of the real shell (ctest `tst_shell_qml`; desktop builds) |
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
  import/export with prepared notebooks.
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
