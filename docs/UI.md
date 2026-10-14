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
  user overrides it in settings.
* Typography: system sans for UI; editor font selectable (serif/sans), comfortable line height,
  configurable max text width.
* Outliner look like Logseq: round bullets, thin vertical guide lines per nesting level, fold
  arrows on hover, clicking a bullet zooms into the block (breadcrumb to zoom out).
* Touch (Android): targets ≥ 44 px, bottom navigation, drawer, toolbar above the keyboard.

## Window layout

* **Wide (≥ 900 px):** `SplitView`: left sidebar (navigation: Dziennik, Kalendarz, Zadania,
  Wszystkie strony, Tagi, Graf, Szukaj; Ulubione; Ostatnie; folder tree; sync status footer) |
  main `StackView` (current view with back/forward history) | optional right panel (backlinks,
  outline, mini calendar).
* **Narrow (< 900 px):** sidebar becomes a `Drawer` (swipe from the left edge); bottom navigation
  bar (Dziennik, Kalendarz, Szukaj, Zadania, Menu); floating quick-capture button; Android back
  key / gesture pops the navigation stack (closing the app only from the home view).
* Keyboard (desktop): Ctrl+K command palette, Ctrl+O quick switcher, Ctrl+Shift+F search,
  Alt+←/→ back/forward, Ctrl+J today, Ctrl+N new page, Ctrl+E toggle source mode.

## Navigation

`AppController` keeps a stack of routes (`{view, path, line, blockId, date, ...}` as
`QVariantMap`); QML `StackView`/`Loader` shows the view component for the top route. Views:
`JournalsView` (home), `PageView`, `CalendarView`, `TasksView`, `GraphView`, `SearchView`,
`AllPagesView`, `TagsView`, `SyncView`, `ImportExportView`, `HistoryView`, `TrashView`,
`SettingsView`, `OnboardingView` (first run: pick or create a vault, optional demo vault).

## The block editor (heart of the app)

A page is edited as a list of **blocks** (Logseq model), backed by `BlockDocument` in the core
(each block keeps its raw Markdown so untouched blocks are written back byte-for-byte):

* `BlockListModel` (C++, `QAbstractListModel`) exposes visible blocks with roles: `raw`,
  `html` (rendered by the core `InlineRenderer`), `depth`, `kind`, `headingLevel`,
  `hasChildren`, `collapsed`, `taskStatus`, `blockId`, `lang`, `calcResults`, `tableModel`, …
  Zoom (show one block's subtree) and folding are model-level.
* `qml/editor/BlockEditor.qml`: a `ListView` of block delegates. An unfocused block shows its
  rendered rich text (`Text` with `textFormat: Text.RichText`, links handled by
  `onLinkActivated`: `page:`, `tag:`, `block:`, `ext:`, `task:` URLs). Clicking it switches that
  block into editing: a `TextArea` with the raw Markdown and a C++ `MarkdownHighlighter`
  (`QSyntaxHighlighter` via `QQuickTextDocument`).
* Keys (handled in QML, executed by `PageController` on the model): Enter = split block at the
  cursor (continues task checkboxes), Shift+Enter = soft line break, Backspace at start = merge
  with previous, Tab/Shift+Tab = indent/outdent with children, Alt+↑/↓ = move block with
  children, ↑/↓ at first/last line = move focus to the previous/next block, Ctrl+Enter = cycle
  task status, Ctrl+. = fold, Esc = leave editing (select block).
* Autocomplete popup while typing: `[[` pages, `((` blocks (creates the target's block id),
  `#` tags, `/` slash commands (Zadanie, Przypomnienie…, Alarm…, Termin…, Powtarzaj…,
  Tabela, Obliczenia, Zapytanie, Nagłówek, Cytat, Callout, Kod, Szablon…, Osadź, Data),
  natural-language dates after `⏰ `/`📅 `/`@` (Polish and English).
* Special block delegates: code (monospace), `calc` (input lines with right-aligned results),
  table (grid editor with formula results; edits rewrite the Markdown table), `query` (live
  results with checkable tasks), callout (coloured box), embeds/images (`![[...]]`), headings,
  horizontal rule, frontmatter (collapsed "Właściwości" panel).
* Chips inside rendered text: 📅 due (red when overdue), ⏳ scheduled, ⏰ reminders (amber,
  bell for alarms), 🔁 recurrence in Polish words. Clicking a ⏰ chip opens the reminder dialog
  (edit / snooze / delete).
* Source mode (Ctrl+E): one `TextArea` with the whole file and highlighting.
* Mobile toolbar above the keyboard: outdent, indent, move up/down, task, `[[`, `#`, `/`,
  date, reminder, undo, redo, hide keyboard.

## Shared components (`qml/components/`)

`BButton`, `BIconButton`, `BTextField`, `BSwitch`, `BCheckBox`, `BChip`, `BTabBar`,
`BSegmented`, `BDialog` (bottom sheet on narrow windows), `BMenu`, `BToast` (+ host),
`BEmptyState`, `BDatePicker` (month grid, Polish names, natural-language input with live
preview "→ czw, 15 paź 09:00"), `BReminderDialog` (date/time, alarm switch, recurrence picker,
quick picks: za 10 min, za godzinę, dziś 19:00, jutro 8:00, w poniedziałek 9:00), `BDiffView`,
`BMergeView` (per-conflict choice: moja / ich / obie / ręcznie), `BCommandPalette`,
`BQuickSwitcher`. Icons: SVGs in `assets/icons/` (Lucide, ISC licence) via `BIcon`.

## Strings

`qsTr()` everywhere with English source text; Polish translation in `i18n/brulion_pl.ts`
(default UI language). Plurals with `qsTr("%n notes", "", n)`.
