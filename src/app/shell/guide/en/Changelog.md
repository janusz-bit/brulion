---
tags: [documentation]
aliases: [Changes, Version history, Release notes]
brulion: document
---
# Changelog

Here you'll find what changed in every version of Brulion, newest first. Full release notes with downloads are on GitHub: https://github.com/janusz-bit/brulion/releases

## 0.14.2 — November 7, 2026

**Changes**
- **Reading mode on the top bar** — the **book** button that switches reading mode moved from a page header's buttons and the **More** menu to the top bar, on pages and the journal at every width; it is highlighted while reading mode is on. The page menu (**⋯**), the palette, **Ctrl+Shift+E** and *Settings → Editor* switch it as before. See [[Document and outline#Reading mode|Reading mode]].

**Fixes**
- The page icon dialog shows its emoji again — the grid of choices was blank.
- A folder in the top bar's path (a page in a folder) opens that folder in All pages again, not the list of all pages.
- Switching a journal day to source mode (Ctrl+E) opens it as a page in source mode again.
- **More export options…** in a page's export dialog opens the Import and export view with that page and format chosen again.
- Going back to a page scrolled only within its header (above the first block) returns to the same place.

## 0.14.1 — November 7, 2026

**Note**
- Version 0.14.0 was not published: its tests found that the scroll bar's new track, which a press pages, counted in the check of touch target sizes as a target of its own, 9 pixels wide. A scroll bar is no such target — the wheel, the keys and dragging the page do what it does — so the check now leaves its track out, as it does the bar. Nothing changes in the app. Everything new in 0.14.0, described below, is available from this version on.

## 0.14.0 — November 7, 2026

**New**
- **Reading mode** — pages and the journal for reading only: a tap or click follows links, opens pictures, PDFs and embedded notes, folds and zooms in, but never starts editing, so on a phone the keyboard doesn't pop up while you read and nothing changes a note by accident. You switch it with the **book** button next to the star in a page's header, “Reading mode” in the page menu (**⋯**), in the **More** menu or in the palette, **Ctrl+Shift+E**, or in *Settings → Editor*; Brulion remembers it on this device. Selecting and copying, find, a table's filter, the version history, export and print still work; whatever would change the note shows a notification with **Edit**, which turns reading mode off. See [[Document and outline#Reading mode|Reading mode]].
- **Wheel scrolling like in a web browser** — on a computer each notch of the mouse wheel scrolls the same distance (three lines), smoothly, however fast you turn the wheel; before, the distance depended on the speed, and quick notches scrolled less. A press on the scroll bar's track beside the thumb scrolls a screen towards the pointer, smoothly, and repeats while you hold it; Shift+press jumps there as before. Touchpad scrolling stays as it was.

**Changes**
- **Phone: the bottom navigation holds views only** — by default Journal, Pages (all pages and folders), Search, Tasks and Calendar. Quick capture is the **+** in the top bar, the sidebar opens with the menu button (three lines) or a swipe from the left edge, and the top bar's search field shows only when the bottom bar has no Search. The bar has the page's colour with a thin edge on top. You choose its five views in *Settings → Appearance → Layout*; views you chose before stay, with a fifth added. See [[Getting started#Sidebar|Sidebar]].
- **Calendar month on phones: icons instead of dots** — a day shows a small icon for each kind of open entry, most urgent first: a red exclamation mark — overdue, an amber bell — a reminder or alarm, a blue calendar — due, a violet hourglass — scheduled. Done entries get no icon, and a short grey line under the day's number marks a journal entry. See [[Journal, calendar and templates#Calendar|Calendar]].
- **Smoother scrolling of long pages** — a page of up to 1000 blocks (300 on phones and in the browser) creates all its blocks when it opens — the ones on screen at once, the rest in the background — so scrolling it no longer builds and drops blocks on the way. It takes a little more memory, about 0.1–0.2 MB a block.
- **A smaller app** — the Android app is 93 MB instead of 106 MB, the browser version 55 MB instead of 61 MB (17 MB to download instead of 18.5 MB) and the Linux program 68 MB instead of 78 MB.
- **Graph: more visible links** — the lines between notes in the graph and in the local graph are stronger and never thinner than one pixel, so in a large graph they no longer fade into the background.
- The side panel's tab on the right edge of a phone shows only its arrow, without the number of backlinks; the backlinks are listed in the panel.

## 0.13.1 — November 7, 2026

**Note**
- Version 0.13.0 was not published: its tests found that text written into a PDF with the new built-in fonts lost some characters — an em dash between words, “fi” — when it was copied or searched. Fixed in this version (below). Everything new in 0.13.0, described below, is available from this version on.

**Fixes**
- The built-in fonts no longer join letters into ligatures or swap a dash for a contextual one: text in PDFs made with them keeps every character when you copy, search or select it.

## 0.13.0 — November 7, 2026

**New**
- **The “Nocny” look** — the new default look: dark and dense, on true black (OLED screens save battery), with a **coral** accent; in the light theme the coral is darker. Coral is a new accent colour in *Settings → Appearance*; red and the other colours stay there. Reminders and alarms (`⏰`) are amber whatever the accent, so they never look like the accent or like overdue tasks in red. See [[Getting started#Appearance|Appearance]].
- **Built-in fonts** — Brulion brings its fonts: Inter for the interface, Literata for notes (the “Serif” choice) and JetBrains Mono for code, formulas and dates. Notes look the same on every device and in the browser; any installed font can still be chosen.
- **Side panel in sections** — the side panel folds into sections, each with its count: for a page **Outline**, **Backlinks**, **Unlinked mentions** and **Local graph** (the pages linked to and from it); for a day of the journal **Day plan** (the day's reminders and dates by the hour, with coloured bars), **Tasks** (open tasks due by that day, overdue ones in red), **A year ago** (that day's journal a year before), **Backlinks** and **Calendar**. A click on a section's title folds or unfolds it. See [[Links and backlinks]].
- **Side panel on phones** — a small tab on the right edge, with the number of backlinks, or the panel button in the top bar opens the side panel as a drawer from the right; a tap on the dimmed page, a swipe to the right or Back closes it. Android's back gesture leaves the tab alone.
- **Status bar** — on a computer a thin bar along the bottom of the window shows the sync status (green “synced”), the edited block (“Block 3 of 12”), the word count and the command palette's key.

**Changes**
- Tabs are square, with a coral edge on top of the active one.
- *All pages*: the journals folder (a journal's breadcrumb leads there) always lists its daily notes, in date order, and its row counts them.

## 0.12.1 — November 6, 2026

**Note**
- Version 0.12.0 was not published: on Windows, the PDF viewer's self-test saw a PDF reopened at page 2 end up on page 3 — fixed in this version (below). Everything new in 0.12.0, described below, is available from this version on.

**Changes**
- **Calendar from the tray icon with one click on Linux** — a click on Brulion's tray icon now opens the calendar on every desktop (before, only on Windows); *Settings → Application → Click on the tray icon → Window* brings back the window. Under Wayland on KDE Plasma (and other desktops with the layer shell) the calendar opens in the bottom-right corner of the screen, next to the panel, instead of wherever the desktop put it. See [[Journal, calendar and templates#Calendar from the tray icon|Calendar from the tray icon]].

**Fixes**
- The PDF viewer: a page jumped to before the real page sizes were read (a PDF reopened at its page, on a slow computer) stays the current page. Before, with pages of different sizes the view could end up on another page, and Back didn't return to the right one.

## 0.12.0 — November 6, 2026

**New**
- **Subtasks under their task** — the **Tasks** view lists a task's subtasks (the `- [ ]` items indented below it) under it, indented by depth and in the note's order, done ones too, in the task's section. A subtask more urgent than that section — overdue under a task for later, or open under a done one — stays on its own in its own section. Filters apply to subtasks as to any task. The home-screen widget on Android keeps a flat list. See [[Tasks#Subtasks|Subtasks]].
- **Folding subtasks** — a task with subtasks folds them with the arrow before its progress counter (**3/5**) or with Left / Right. Folding is only how the view looks: nothing is written to the file.
- **Documentation in English** — the in-app documentation is now also in English, and the documentation (“Brulion Documentation”) and the “Sample notebook” follow the interface language; there is a new English sample notebook. The English interface uses English examples (habits such as `habit:: every day`, repeat rules).

**Changes**
- On phones and narrow windows the ☰ button is always the first in the top bar, also with the keyboard open. On Android there is no back arrow: the system Back button or gesture goes through the same history.
- Smoother scrolling of pages: a block coming onto the screen lays out its text once instead of two to four times, and the scroll bar does about a third of the work per frame. Resizing the window on a page with 300 blocks takes 26 ms instead of 34 ms.

## 0.11.2 — November 6, 2026

**Note**
- Version 0.11.1 was not published: on Windows, the same editor test as in 0.11.0 still checked in one place whether the edited block was back on screen before the editor had scrolled it there, and the new test for deferred calls assumed an order of events that Windows doesn't guarantee — fixed in this version. Everything new in 0.11.0, described below, is available from this version on.

## 0.11.1 — November 5, 2026

**Note**
- Version 0.11.0 was not published: the release was held up by an editor test which, on slower build machines (Windows, Linux with extra memory checks), saw that after leaving a zoomed-in block the edited block didn't come back on screen right away. The editor's deferred scroll ran after other scheduled work instead of before it — fixed in this version. Everything new in 0.11.0, described below, is available from this version on.

## 0.11.0 — November 5, 2026

**Changes**
- The whole interface (more than 18,000 QML bindings and functions) is now compiled to C++ when the app is built, instead of being run by a JavaScript interpreter while the app is running. Nothing looks or works differently; it takes load off the processor when opening views, scrolling and typing, especially on slower phones.

## 0.10.3 — November 5, 2026

**Note**
- Version 0.10.2 was not published: the release was held up by two tests — an interface test where a tooltip covered a button, and a check of the command line in the Nix package — fixed in this version. Everything new in 0.10.0–0.10.2, described below, is available from this version on.

## 0.10.2 — November 5, 2026

**Note**
- Version 0.10.1 was not published: the release was held up by a stylus test in the browser that assumed a light drawing sheet, while the default look is now dark — fixed in this version. Everything new in 0.10.1 and 0.10.0, described below, is available from this version on.

## 0.10.1 — November 5, 2026

**Note**
- Version 0.10.0 was not published: the release was held up by two interface tests which, on slower build machines (Windows, Nix), clicked before the menu and the list of sections were ready — fixed in this version. Everything new in 0.10.0, described below, is available from this version on.

**New**
- **Intro on first launch** — a few steps you can skip: language, look, the permissions reminders need on this device (with a test notification) and the settings of your first notebook. You can run it again from **Settings → General → Run the intro again**. After updating from an earlier version the intro doesn't show up.

**Changes**
- The default interface language is English, and the default look is dark, with a black background. You can pick Polish in the intro or in **Settings → General**.

## 0.10.0 — November 5, 2026

**New**
- **Reminders from other devices on Android** — a reminder added on your computer (also by an AI agent through `brulion-cli`) rings on your phone even when Brulion is closed. Every so often Android checks the last opened notebook in the background — syncing it first if you use Nextcloud — and sets what it finds: new reminders are added, and deleted or moved ones disappear or move. How often (15 min, 30 min, an hour or never) is up to you in **Settings → Reminders → Check notes in the background**; the last check is shown under the setting. See [[Reminders and alarms#Reminders from other devices|Reminders from other devices]].
- **Views bar along the left edge** — the notebook at the top (its first letter switches notebooks), the view icons with tooltips below it, and the sync status and Settings at the bottom. It stays when you hide the pages panel; on a phone it's the left column of the menu. You can change the order of the pages panel's sections (Favorites, Recent, Pages) in **Settings → Appearance → Layout**. See [[Getting started#Sidebar|Sidebar]].
- **Search or type a command** — the field in the top bar opens the quick switch to a page; `>` at the start switches to commands, and the last item of the list searches for words inside your notes. See [[Getting started#Search and navigation|Search and navigation]].
- **New More menu** — the page's actions as icons (Favorite, Rename, Move, History), the **Mode** switch (Document, Outline, Source), **Open in** on a computer (new tab, window, side panel, split), and the notebook's views in the **Notebook** submenu.
- **F-Droid repository** — Brulion has its own F-Droid repository (https://janusz-bit.github.io/brulion/): add it once in F-Droid and new versions arrive on their own.
- **Browser version in a single file** — open the `brulion-web-….html` file from a release straight from your disk, with no server and no internet; your notes stay in the browser even after you download a newer version of the file.

**Changes**
- Nextcloud sync waits a moment when the same notebook is being synced by the background check on Android, and retries on its own.

## 0.9.1 — November 5, 2026

**Note**
- Version 0.9.0 was not published: the release was held up by bugs found by tests and during the build, fixed in this version — among them printing and spell checking in the AppImage and building the Windows version. Everything new in 0.9.0, described below, is available from this version on.

## 0.9.0 — November 4, 2026

**New**
- **Tabs, split view and separate windows** — several pages open at once, each in its own tab with its own history (back / forward) and scroll position. **Ctrl+T** opens a new tab, **Ctrl+W** closes it, **Ctrl+Tab** / **Ctrl+Shift+Tab** and **Ctrl+1…9** switch between them; you can open a page from the sidebar or a list in a new tab with the middle mouse button or **Ctrl+click**, and a link with the middle button or from its menu. **Split view** shows a second page next to the first, and on a computer **Open in new window** gives a page its own window. Tabs are remembered for each notebook; on a phone you switch them with the little square showing the number of tabs in the top bar. See [[Keyboard shortcuts#Tabs, split view and windows|Tabs, split view and windows]].
- **Spell checking** — misspelled words in the block you're editing are underlined, in Polish and English. Right-click (on a phone, tap the word) shows suggestions, **Add to dictionary** and **Ignore**; code, links, addresses, tags, dates and formulas are skipped. On a computer the checking is done by Hunspell with dictionaries bundled with the app, on Android by the keyboard's system spell checker; the browser has no spell checking. **Settings → Editor**. See [[Spell checking|Spell checking]].
- **Link preview** — hovering the mouse over `[[page]]`, `[[page#heading]]` or `((block))` shows a popup with its content; clicking the name opens the page, and links in the popup work. There's no preview on a touch screen (a long press opens the link's menu). See [[Links and backlinks#Link preview|Link preview]].
- **Weekly and monthly notes** — alongside daily ones: `journals/2026-W45.md` (ISO week) and `journals/2026-11.md`, with their own folder, file name format and template (variables `{{week}}`, `{{month}}`, `{{end}}`). Open them with the week number or the month title in the calendar, **Ctrl+Alt+W** and **Ctrl+Alt+M**, the command palette, and `brulion-cli week` and `month`. See [[Journal, calendar and templates#Weekly and monthly notes|Weekly and monthly notes]].
- **Habits** — a list item with `habit:: every day` (or `mon, wed, fri`, `3 times a week`, `every month on the 15th`) is a habit, and checking it off is a task with `#habit` in the daily note. The **Habits** view shows today's check-offs, the current and longest streak, the success rate and a month grid; `brulion-cli habit list`, `tick` and `skip`. See [[Habits|Habits]].
- **Task dependencies** — `🆔 abc123` and `⛔ abc123` from Obsidian Tasks work: a task waits until the task with that ID is done, and Tasks, query results and the editor show what it's waiting for. **Add dependency…** in the task's menu; in queries `is:blocked`, `-is:blocked` and `is:blocking`. A task with subtasks has a “3/5” counter. See [[Tasks#Task dependencies|Task dependencies]].
- **Time tracking on a task** — **Start timer** in the task's menu records time in the task's `:LOGBOOK:` drawer, like Logseq; one timer runs at a time, even after a restart. **Time spent** in Tasks sums up days, pages, tags and tasks, and `brulion-cli task start`, `stop` and `time` do the same from the command line. The **Log task status changes** setting (off by default) adds every status change to `:LOGBOOK:`. See [[Tasks#Time tracking|Time tracking]].
- **Sorting, filtering and CSV in tables** — the column menu has **Sort ascending** and **Sort descending** (numbers, dates, text in Polish order; summary rows stay in place) and **Filter rows**, which hides rows in the view only. You can copy or save a table as CSV, and `/Table from CSV…` inserts a table from a file. See [[Tables and charts|Tables and charts]].
- **Charts** — a ```` ```chart ```` block draws a bar, line or pie chart from the table above, a `calc` block or a query, in the theme's colors; `/Chart` inserts one. HTML export includes the chart as an SVG image. See [[Tables and charts|Tables and charts]].
- **Searching the journal across many days** — **Ctrl+F** in the journal stream counts matches across all days (“3 of 12 · 4 days”), **Enter** goes to the next day with a match, and “Replace all” changes all days and is undone with a single **Undo**. See [[Getting started#Search and navigation|Search and navigation]].
- **Exporting a single page, printing, PDF and Word** — the page menu and the command palette have **Export page…** (Markdown, Markdown with attachments in a ZIP, HTML, PDF, Word, Pandoc; saved to a file, on Android also to Downloads, in the browser as a download, Markdown and HTML also to the clipboard) and **Print…** (**Ctrl+P**: the system print dialog on a computer and on Android, the browser's own printing in the browser). Brulion makes PDF (with embedded fonts, A4 or Letter depending on your region) and Word (DOCX: headings, lists, tasks, tables, pictures, links) by itself, without Pandoc or LaTeX; both formats are also available when exporting the whole notebook. See [[Import and export#A single page|A single page]].
- **PDF viewer: text, table of contents, links, passwords** — select a PDF's text with the mouse or a long press and copy it (**Ctrl+C**). The table of contents takes you to a chapter, links in the PDF work (`http`, `https` and `mailto` addresses), a password-protected PDF asks for the password (it's never stored anywhere), and **Two pages side by side** shows pages in pairs. Existing annotations are visible. On Android, selecting, links and passwords aren't available yet, and the table of contents is only in builds with poppler (Linux, NixOS). See [[Document and outline#PDFs|PDFs]].
- **Other attachments in another app** — **Open in another app** now also works for Office and OpenDocument documents, EPUB, RTF, audio and video; the file is checked by its extension and content, so a program renamed to `.docx` won't be launched.
- **Page icon and cover** — **Add icon…** in the page menu gives a page an emoji or icon, shown before the title, in the tree, in favorites, in the page switcher and before links to it. **Add cover…** puts a picture from the notebook or from the internet as a banner at the top of the page. Stored the same way as the Iconize and Banners plugins for Obsidian (`icon`, `banner`). See [[Tags and properties#Page icon and cover|Page icon and cover]].
- **Colored highlights** — **Highlight…** in the editor menu (on a phone, the highlighter in the toolbar) highlights the selection in one of 9 colors, changes the color or removes the highlight. Stored as `<mark style="background: …">`, like the Highlightr plugin for Obsidian. See [[Document and outline#Colored highlights|Colored highlights]].
- **Statistics and focus mode** — the word count in the corner of the page (with a selection — the selection's), and words, characters and reading time in the page menu. **F8** turns on focus mode: no sidebar, top bar or bottom navigation, just the text in the middle. **Typewriter scrolling** keeps the line you're typing in the middle of the screen. See [[Document and outline#Statistics and focus mode|Statistics and focus mode]].
- **Custom keyboard shortcuts** — **Settings → Keyboard shortcuts** changes the shortcut of any command in the palette (including the ones that have none by default), detects shortcuts already used by other commands and restores the defaults. See [[Keyboard shortcuts#Custom shortcuts|Custom shortcuts]].
- **Aligning and distributing in drawings** — the **Style** panel aligns the selection to the left, center, right, top or bottom and spaces three or more elements evenly horizontally or vertically (**Ctrl+Shift+arrows**, **Alt+H**, **Alt+V**). See [[Drawings#Selection style|Selection style]].
- **Text in every shape** — the triangle, star, speech bubble and the other shapes in the list hold text in the middle, like the rectangle; excalidraw.com shows it too. See [[Drawings#Shapes and arrows|Shapes and arrows]].
- **Clipboard with excalidraw.com** — **Ctrl+C** / **Ctrl+V** (or **Copy** and **Paste** in the menu) move elements between Brulion and excalidraw.com in both directions. See [[Drawings#Copy and paste (also with excalidraw.com)|Copy and paste]].
- **PDF pages in the drawing editor** — **Previous** / **Next PDF page** (**PgUp** / **PgDown**) in a drawing on a PDF page opens the drawing of the neighboring page, and if there isn't one, creates it and inserts it into the note next to the current one. See [[Drawings#Notes on slides and PDFs|Notes on slides and PDFs]].
- **Pictures in drawings from Obsidian** — pictures from the “Embedded Files” section of `*.excalidraw.md` drawings show up in the editor and in previews, and HTML and Pandoc export show such drawings as pictures. See [[Drawings#Drawings from Obsidian (Excalidraw)|Drawings from Obsidian]].

**Changes**
- Tags and dates have a rounded background also in backlinks, Tasks, embedded pages, tables and query results, and long rows of these lists end with “…” (formatted text used not to be cut off).
- **Ctrl+click** in backlinks and in the link preview opens the page in a new tab; the side panel is in the menu and under **Ctrl+Enter**.
- The notebook's index is rebuilt once from scratch after the update (new data about tasks and habits).

## 0.8.1 — November 3, 2026

**New**
- **Mermaid diagrams** — a ```` ```mermaid ```` block is drawn in the note as a diagram in the theme's colors, light or dark: flowcharts (`flowchart` or `graph`, directions `TD`, `LR`, `BT`, `RL`), sequence diagrams (`sequenceDiagram`) and pie charts (`pie`). `/Diagram` inserts a ready-made flowchart to adapt, and clicking a diagram shows its source with a live preview; if there's an error, you see which line it's on. Brulion draws diagrams by itself, without the internet and without running anything from the note, so a diagram that came through sync or an import is safe (`click` commands and links are skipped). HTML export includes diagrams as SVG images, while the file keeps just the source. Other diagram types show their source for now. See [[Diagrams|Diagrams]].

**Note**
- Version 0.8.0 was not published: the release was held up by errors in tests and in the Android build, not in how the app works. Everything new in it, described below, is available from this version on.

## 0.8.0 — November 3, 2026

**New**
- **Automatic backups** — Brulion backs up your notebook by itself: a plain ZIP archive with your notes, attachments and the notebook's settings, which you can unpack with any program. Daily or weekly (if nothing has changed, there's no new backup), keeping as many backups as you choose; the oldest one is removed only after the new one has been saved successfully. Backups go to the app's private folder or to a folder you pick (e.g. on a USB stick). **Restore** first shows how many files it will replace, how many will come back and how many will go to the trash, then makes a safety backup of the current state and pauses Nextcloud sync while restoring. All of this is in **Settings → Backups**; automatic backups are on by default, and off in the browser. See [[Backups|Backups]].
- **Find and replace in a note** — **Ctrl+F** opens a search bar in the open note, and **Ctrl+H** opens it with the replace field right away (on a phone: page menu → **Find and replace…**). Matches are highlighted, **Enter** and **Shift+Enter** move between them, and the “Aa”, “ąa” and “Word” buttons match case, Polish letters and whole words. “Replace all” is undone with a single **Ctrl+Z**. See [[Getting started#Search and navigation|Search and navigation]].
- **Replace across the whole notebook** — **Replace in notebook** (the **More** menu, the command palette, “Replace…” in search) shows every match as “before → after”; uncheck what you don't want changed, and a match inside a link is marked, because the link would start pointing somewhere else. After replacing, **Undo** restores the notes, and their previous versions are in the version history too. See [[Getting started#Search and navigation|Search and navigation]].
- **Clickable parts of a nested tag** — in `#work/reports`, clicking `#work` opens the parent tag `work`, and clicking `reports` the whole tag `work/reports`; the tag stays a single label. See [[Tags and properties#A tag is a link to a page|A tag is a link to a page]].
- **Delete with undo** — after moving a page, attachment or folder to the trash from the **More** menu or from **All pages**, the notification has **Undo**. In the page tree the `Delete` key deletes, and the command palette has **Move to trash**. The command line has `brulion-cli file delete`, which also moves attachments to the trash. See [[Sync#Version history and trash|Version history and trash]].

## 0.7.1 — November 2, 2026

**Note**
- Version 0.7.0 was not published: the release was held up by a bug in one of the tests, not in the app. Everything new in it, described below, is available from this version on.

## 0.7.0 — November 2, 2026

**New**
- **More shapes in drawings** — the shape list has 11 new items: **Triangle**, **Right triangle**, **Parallelogram**, **Trapezoid**, **Pentagon**, **Hexagon**, **Star**, **Block arrow**, **Speech bubble**, **Heart** and **Cloud**. In the file they're ordinary closed Excalidraw lines, so excalidraw.com and Obsidian show them too and let you edit them. See [[Drawings#Shapes and arrows|Shapes and arrows]].
- **Resizing and rotating with handles** — the selection has squares on its frame for resizing and a circle for rotating, keeping proportions or in 15° steps with `Shift`. The **Select** tool works as a lasso or a rectangle, and **More** has **Rotate by 90°**, **Flip horizontally** (`Shift+H`), **Flip vertically** (`Shift+V`), **Group** (`Ctrl+G`) and **Ungroup** (`Ctrl+Shift+G`). See [[Drawings#Resizing and rotating|Resizing and rotating]].
- **Selection style in drawings** — the **Style** button above the selection: fill and its pattern, line width and type, corners, line start and end, text size and opacity. Each choice is one undo step. See [[Drawings#Selection style|Selection style]].
- **Table rows and columns** — the cell menu (right-click; on a phone, **More** in the bar above the keyboard) has **Row** and **Column**: inserting, moving, deleting and column alignment. In a cell, **Ctrl+Enter** inserts a row below, **Alt+↑/↓** moves the row, and **Alt+←/→** the column. Formulas keep pointing at the same cells: after inserting a row above `B2`, the reference changes to `B3`, and a reference to a deleted row or column becomes `REF!`, which the cell shows as `#ERR`. See [[Calculations#Tables with formulas|Tables with formulas]] and [[Keyboard shortcuts#Table cell|Table cell]].
- **Clean up** — a new view (the **More** menu and the command palette) with notes and attachments that no link, tag or query leads to. Choose what to move to the trash: notes, attachments or individual files, and **Undo** brings them all back. Journals, templates, favorites and hidden files aren't listed there. See [[Sync#Version history and trash|Version history and trash]].
- **New version notice on Android and Windows** — once a day Brulion asks GitHub about the latest release, and if it's newer, a notice appears under the top bar. **View on GitHub** opens the release page, and the × hides the notice until the next version. GitHub sees your device's address; nothing about your notes is sent. You can turn this off in **Settings → Application → Check for new versions**. See [[Getting started#New version|New version]].

## 0.6.3 — November 2, 2026

**New**
- **Custom drawing background color** — the **Drawing background** menu (on a phone, under **More**) has a **Background colour…** item: any color from the grid or typed in as `#fff9db`, with a preview of ink on that background and a warning when the ink would be hard to see. The paper's lines and grid adapt to the color. The color is saved in the drawing, can be undone, and when versions from two devices are merged, the changed one wins. See [[Drawings#Light and dark background|Light and dark background]].

## 0.6.2 — November 2, 2026

**New**
- **Any file as a link** — the `/File…` command inserts a `[[report.docx]]` link to a file of any type at the cursor; you can pick several files at once, also in the browser version. A file from outside the notebook goes to the attachments folder (an identical file that's already there isn't copied a second time), while a file from the notebook stays where it is. A note, picture or PDF opens on click as usual, and other files open read-only in Brulion, as text. See [[Document and outline#Images and files|Images and files]].

## 0.6.1 — November 1, 2026

**New**
- **PDF preview** — a PDF embedded in a note (`![[lecture.pdf]]`, also `![[lecture.pdf#page=3]]` as in Obsidian) shows a thumbnail of the page on a white sheet, with the file name and the number of pages. Clicking it opens the in-app PDF viewer: scrolling through pages, zooming (with fingers, `Ctrl` + wheel, buttons, fit to width, double tap), “Page 3 of 12” with a jump to a page, searching the text ignoring Polish letters and case (on Android from version 15), **Open in another app** and **Draw on this page** — a drawing on the page you're reading goes into the note right under the PDF, so you can take notes on slides straight from the PDF. The link `[[lecture.pdf#page=3]]` opens the PDF at that page, and **Back** or `Esc` closes it. A password-protected PDF shows a padlock instead of an error. In the browser version the PDF opens in a new tab with the browser's built-in PDF viewer. See [[Document and outline#PDFs|PDFs]].
- **Clickable parts of a link to a page in a folder** — in the link `[[Company/Adam Nowak]]` each part of the path is a separate link, like namespaces in Logseq: **Company** opens the page “Company” (by path, name or alias, like an ordinary link), and if there's no such page — the list of pages in the **Company** folder in **All pages**; if there's neither a page nor a folder, clicking creates the page like a link to a page that doesn't exist yet. **Adam Nowak** opens the page `Company/Adam Nowak`, as before. The parts are separated by a gray slash, and the part under the cursor is underlined (on a phone — under your finger). Right-click or a long press on a part opens its menu (**Open** or **Show folder**, **Open in side panel**, **Copy link** or **Copy folder path**). A link with its own text (`[[Company/Adam Nowak|Adam]]`) stays a single link, and while you edit the block the text looks as before. It also works in query results, tables, embedded pages, properties, **Tasks** and backlinks. From the keyboard: **Alt+Enter** in the block being edited opens the link (or link part) under the cursor, **Alt+Shift+Enter** — in the side panel; screen readers see each part and each link in the text as a separate link. See [[Links and backlinks#Links to pages in folders|Links to pages in folders]].

**Fixes**
- Clicking a PDF (or another attachment that isn't a picture or a note, e.g. DOCX or ZIP) no longer opens its content in the editor as text — a typed character could overwrite the file. Such files now open read-only.
- In the browser version, a PDF embedded in a note no longer takes up memory with a copy of the whole file.
- **Android: empty window at startup** — when the system recreates Brulion's window just as the app is starting (e.g. a change of the system theme colors right after launch, or right after switching the phone on), the window no longer stays empty: Brulion restarts itself and opens the notebook, including the note from a tapped notification or the window for text shared from another app.

## 0.6.0 — November 1, 2026

**New**
- **Dark background for drawings** — the **Drawing background** button in the drawing editor (on a phone, in the **More** menu) has three options: **Like the app** (the default: the drawing darkens and lightens along with Brulion's theme, instantly), **Light** and **Dark**. On a dark sheet the colors are adjusted like in Excalidraw's dark mode — light ink, readable colors, dimmed paper lines, and the highlighter stays visible. Colors stored in the drawing don't change, and your choice is saved in the file. Drawing previews in notes and the picture view follow the theme too, and a drawing set to follow the theme also darkens in Obsidian with a dark theme and in a browser that uses a dark theme. See [[Drawings#Light and dark background|Light and dark background]].
- **Editing page properties** — the **Properties** panel now has fields by type: text (with working links), number (also “3,5” and “1 200”), checkbox, date (also “tomorrow”, “on Friday”), date and time, list, tags and aliases. You can add properties (**Ctrl+;** or **Add property** in the page menu — also on a page with no properties at all), rename them or change their type, and delete them without editing the text; suggestions show keys and values from the whole notebook. You can also add and remove tags and aliases in the page header. Only that one property changes — comments, order and the formatting of the rest of the header stay as they were — in the YAML header and in Logseq style (`key:: value`). Every change can be undone, and an expanded panel stays expanded. See [[Tags and properties#Editing in the Properties panel|Editing in the Properties panel]].
- **Shapes, arrows, text and lasso selection in drawings** — new tools in the drawing editor: **Shapes** (rectangle, ellipse, diamond, line and arrow; with `Shift` a square, a circle and angles in 15° steps), **Text** in four sizes (also labels inside shapes) and **Select** — a lasso or a tap selects elements, which you can move, duplicate, recolor, bring to front or send to back, or delete from the bar above the selection. Shortcuts as in Excalidraw (V, R, O, D, L, A, T, `Ctrl+D`, `Ctrl+A`, `Delete`, arrows), and each change is one undo step. On a phone held upright the tools get their own row in the toolbar, so they're all on screen. Elements are saved as ordinary Excalidraw elements, so excalidraw.com opens them for further editing. See [[Drawings#Shapes and arrows|Shapes and arrows]] and [[Drawings#Lasso selection|Lasso selection]].
- **Drawings from Obsidian** — `*.excalidraw.md` files of the Excalidraw plugin for Obsidian open in the drawing editor, show up in notes (`![[Drawing.excalidraw]]`, `![[Drawing.excalidraw.md]]`, and also `![[Drawing]]`) and as a picture when opened from the page list, and are saved in the same format: Brulion changes only the drawing's scene and the entries of changed texts, while the frontmatter, the notes on the back of the drawing and the rest of the file stay byte for byte the same — Obsidian will keep opening them. `.excalidraw` files can be edited too. Pictures pasted in Obsidian show up as gray rectangles for now, and drawings with frames are view-only. See [[Drawings#Drawings from Obsidian (Excalidraw)|Drawings from Obsidian]].
- **Open in Brulion and sharing beyond Android** — on a computer you can open `.md` and `.txt` files in Brulion from your file manager (on Windows the installer adds **Open with → Brulion**, on Linux **Open with → Other application → Brulion** — the desktop entry from the Nix package and the AppImage; either way without changing your default program, even on a desktop with no default programs at all). The same **Save to Brulion** window as on Android appears: the file goes into today's journal or becomes a new page, and the original stays untouched; a file from the open notebook simply opens, and several files at once go into one window, also when Brulion is already running. The browser version installed as an app (Chrome or Edge on Android) is in other apps' **Share** menu — text and links go into the same window, also when Brulion is already open in another tab. See [[Journal, calendar and templates|Journal, calendar and templates]].
- **Notes on slides and PDFs** — a drawing can sit on a picture or a PDF page from a lecture: the `/Drawing on a picture or PDF…` command (also `/slide`, `/pdf`) and **Insert a drawing on a picture or PDF…** in the block menu insert a drawing on the chosen picture, and for a PDF — one drawing for each page you pick in the window with page thumbnails (you can also type them, e.g. “1-3, 7”; at most 60 at once). In the drawing editor, the **Paper** menu has **Backdrop from a picture or PDF…**, **Another PDF page…** and **Remove backdrop**. The eraser won't erase the backdrop and the lasso won't select it, on a dark sheet it darkens like a PDF reader's night mode, and its picture is stored in the drawing file, so Obsidian, the browser and excalidraw.com show the slide with your notes. PDFs work on Android, Windows and Linux (Nix package and AppImage), and pictures in the browser. See [[Drawings#Notes on slides and PDFs|Notes on slides and PDFs]].
- **Merging drawings** — a drawing's conflict copy from Syncthing (`Drawing.sync-conflict-….svg`, also for drawings from Obsidian, `*.excalidraw.md`) is shown in the Sync center as two drawings side by side, with a preview of the result and the number of elements in each, and the **Merge drawings** button combines the lines and shapes from both versions into one drawing. Lines erased on only one device come back after such a merge. The previous version stays in the version history and the copy goes to the trash — unless it's a drawing from Obsidian whose copy has different notes (frontmatter, notes on the back): then the copy stays, and the center shows how the notes differ, so you can move them over before deleting it. Nextcloud merges a drawing changed on two devices by itself, element by element, instead of leaving a copy (what was erased on one device disappears). See [[Drawings#Saving and sync|Saving and sync]].

**Changes**
- **A more comfortable text width** — the default text width in the editor is now 680 px (about 85 characters per line instead of about 95), so long paragraphs are easier to read. If you prefer it wider, choose **Wide** or **Full** in **Settings → Editor → Text width**; **Narrow** is now 600 px. A previously chosen **Normal** or **Narrow** switches to the new width, and a custom width stays unchanged.
- **A calmer look** — `#tags` and dates in the text are rounded labels like in the page header (a lighter background with some space inside), so they don't look like a selection, and `code` and ==highlights== have rounded corners. List bullets in a document are the same dots everywhere, and nested lists have thin indent lines like an outline; the indent lines in an outline are more visible, and the page tree in the sidebar and in the drawer has the same lines under the folder arrows. The sidebar, the note and the side panel are separated by a single thin line instead of a double one. In document mode, headings have more space above and below them. Calculations (the `calc` block) use the note's font and size instead of a small fixed-width font, and the digits of the results have equal widths, so they still line up in a column (aligning the inputs with spaces is no longer preserved); the frame appears only while editing. Search results (title and snippet) use a single typeface, quoted blocks in **Links** under the page are the same size as in the side panel, and the counter on the **Links** tab is gray instead of red. The list of the command palette and the quick switcher ends on a whole row and shows its scroll bar right away, and on a computer all its rows have the same height (the description, e.g. the page's folder, sits next to the name); the note behind the palette and behind windows in the middle of the screen fades instead of just darkening. A switch also shows its state with a mark on the knob (a check mark when on, a cross when off), an off switch is more visible (3:1 contrast), and in the dark theme the selected option of an option switch is clearly lighter and option switches have no border.
- **Phone: + Note in the bottom bar and bigger touch targets** — quick capture is a **+ Note** button in the bottom bar (before **Menu**) instead of a round button over the content, so nothing covers the text, the calendar or search results anymore; you can turn it off, as before, in **Settings → Appearance → Layout**. The bottom bar's buttons share its width according to the length of their names, so “Calendar” or “Journal” fit completely even with a larger text size and on narrow phones. The drawer is opened with **Menu** in the bottom bar (or a swipe from the left edge), and the top bar has a menu button (three lines) only when there's no bottom bar: with the keyboard open and in the drawing editor; otherwise the title starts at the edge of the content. List bullets, numbers, day dates in the journal, the page title and the bar under a day are easier to hit with a finger. Rows in the drawer are shorter (44 px), so more pages fit in it, the “⋯” next to pages is quieter, and **Import notes** follows the same rhythm as the other navigation items. The note mode switch (document / outline) is in the page's **⋯** menu instead of next to the title, and a page that starts with its own title has its buttons (star, menu) at the end of its tag row instead of in a separate row above them.
- **Properties panel** — on a phone the key and value sit side by side (as on a computer), so the expanded panel fits all properties and the beginning of the note; when some key is too long for its column (e.g. “last updated”), all keys sit above their values, so none is cut off. The expanded panel has no border, and tags and aliases aren't repeated under the page title then (they're in the panel); the collapsed panel doesn't repeat tags and aliases shown in the header, and shows dates the same way as the expanded one (e.g. “Thu, Oct 29”). Values are the same size as the names (also while editing), rows of lists (tags, aliases, lists) have the same height as the others, pages and people in a list are uniform filled labels (you can tell a page by its icon), and there's more space between the panel and the note's text. On a phone, list labels have no ×: you remove them from the label's menu (long press; for an ordinary list item or alias a tap is enough — like the labels under the page title; both menus are the same), the “+” button never sits alone in a row, and in the dark theme the expanded panel is lighter than the page.
- **Sidebar and tag pages** — the **Favorites** section appears only once you star your first page, and **Recent** skips the page that's open (the tree highlights it). On a tag page, **Tag overview** is the **#** button next to the title, and **Tasks with this tag** is in the page's **⋯** menu (on a computer too), so the note starts higher up. Backlinks under the page aren't repeated when the side panel shows them (the **Links** tab); after closing it or switching to another tab they're back right away.
- **Settings** — rows are separated by thin lines, and on a phone the settings aren't in a frame (the header and the rows start at the same edge). A short choice or button (e.g. **Light background** or **Choose…**) sits next to its description on a phone too, and only those that don't fit beside it (e.g. **Theme**, **Text size**) go under the description at full width, with options as even as possible — so the settings page is shorter.
- **Calendar** — the selected day is a dark circle around the number (as in the date picker), cells no longer have a background for weekends or days of another month, and an alarm in the grid isn't a solid amber bar. Completed entries aren't struck through, just gray with a check mark, and cancelled ones are struck through with a cross — as in **Tasks**, in the month, the week and the day panel. In a day, the entries to do come first, the day's buttons (**Open journal**, **Add task**, **Add reminder**) are an even list under the date, and an empty day plan is a short note instead of a picture.
- **Import from Trilium: a drawing is one file** — a *canvas* note from Trilium now becomes a single SVG drawing file (the picture drawn by Trilium with the Excalidraw scene inside) instead of a picture plus a separate `.excalidraw` file. You can open it in the drawing editor and keep drawing, moving and changing elements; excalidraw.com opens it too, and other programs show the picture from Trilium. Pictures pasted into the drawing in Trilium are inside it, not in separate files. See [[Drawings#Drawings and other apps|Drawings and other apps]].
- **Excalidraw drawings with shapes can be edited** — a drawing from excalidraw.com with shapes, arrows, text or pasted pictures is no longer view-only: Brulion draws it by itself (smoothly, with text in its own font) and you can draw in it, move and change elements. Only drawings with frames or embedded web pages remain view-only. Changes from another device while the drawing is open are now merged element by element (something moved here and there stays moved). See [[Drawings#Drawings and other apps|Drawings and other apps]].
- **Faster opening of large notebooks and less memory** — notes are indexed on several cores at once, and the finished index is saved on the device (in Brulion's data, not in the notebook), so the next time you open the notebook Brulion only re-analyzes the notes changed since last time (it still reads all files to check that). A notebook with 10,000 notes is fully indexed on a computer in under a second instead of about 1.9 s, and the index takes 60% less memory (the whole app: about 340 instead of 510 MB). On a computer and a phone, search is complete as soon as the index is ready. The browser also remembers the finished index (separately from the version history and settings, so if it runs out of space, only the index is lost) and has full search right away on later visits; on the first visit, as before, search catches up a moment after the notes have loaded. `brulion-cli` uses the same index as the app. The first launch after updating Brulion rebuilds the index from scratch. See [[File format|File format]].
- **Phone: bottom navigation comes back after hiding the keyboard** — when you hide the keyboard with the Back button or its own key and keep writing, the bottom bar with Journal, Calendar and Tasks is back on screen, and the toolbar sits right above it (the same goes for the block selection tools). A sliding keyboard covers the navigation and the toolbar slides in along with it, without jumping. Tapping a navigation button ends writing and opens that view. Works in the Android app and in the browser version on a phone. See [[Getting started#Writing on a phone|Writing on a phone]].
- **A smaller app, system file pickers** — the Android version is about 15 MB smaller (about 4.6 MB less to download), the Windows version about 7 MB (the installer 3 MB), the AppImage about 4.5 MB, and the browser version downloads about 2.3 MB less on the first visit, so on a slower connection it opens about a second sooner. File and folder pickers (import, export, “Picture…”, event log, reminder sound, notebook folder) are the system ones everywhere: on Android, on Windows and on Linux with GNOME or KDE as before, and the AppImage shows the desktop's dialog through xdg-desktop-portal on every desktop with a portal, GNOME and XFCE included (without a portal — Brulion's own dialog, in its colors: background, text and accent, dark ones too). In the browser you can pick several files to import or several pictures at once; a file over the limit (512 MB for import, 200 MB for a picture) is skipped without being read, and when the chosen files together would exceed 1.5 GB for import or 512 MB of pictures, the rest aren't read (import tells you about it just like on a computer, skipped pictures are listed). See [[Import and export#Import|Import]].
- **Faster refreshing in large notebooks** — after each save, Tasks, Calendar, reminders, query blocks and Android widgets recompute only the changed note instead of the whole notebook: in a notebook with 10,000 notes this work takes about 6 ms on a computer instead of about 112 ms, so typing with the task list or calendar open no longer stutters when saving. The results are exactly the same as before.
- **Faster undo on long pages** — Ctrl+Z and Ctrl+Shift+Z no longer reload the whole page, they only change the blocks the step affects: on a page with 5000 blocks an undo takes as long as Enter (on a computer about 13 instead of 33 ms). Collapsed blocks, zoom and the open editor stay in place after undo, also when the undo changes their text, and pasted Markdown text, a template or a change of the note mode take up only the changed blocks in the undo history.
- **A smooth graph** — the **Graph** view is laid out in a separate thread and drawn by the graphics card: you can pan it, zoom it and drag pages from the very first moment, and a dragged page follows your finger without delay. A small graph unfolds smoothly over about a second, a large one (a notebook with 10,000 notes, 4000 pages in the graph) appears after about 0.1 s instead of about 1 s, and panning and zooming while it's being laid out respond 2–3 times faster, even on a computer without a graphics card. On a phone the graph animates 60 times a second instead of 30. With the **Reduce motion** setting the graph doesn't unfold on screen: a spinner turns for a moment, and then the finished layout appears. See [[Links and backlinks#Graph|Graph]].

**Fixes**
- **Browser version on a phone: the toolbar sits on the keyboard** — in Brave on Android there was an empty strip between the formatting bar and the keyboard (Brave reports too small a height for the visible part of the page when it hides its bottom bar). Now the browser itself shrinks the page above the keyboard, so the bar rests on it in Brave, Chrome and Firefox (in Chrome and Brave only the browser's own autofill bar sits between them), and the app's top bar no longer escapes off the screen after tapping a block low on the page.
- **Enter in a zoomed-in heading or paragraph** — in an outline, after “Zoom in” on a heading or paragraph, Enter at the end (or in the middle) of the text inserted a block outside the zoom and the cursor disappeared. Now the zoom ends and the new block appears below with the cursor in it; Ctrl+Z goes back to the zoom. In a zoomed-in list item, Enter in the middle of the text moves the rest into a new sub-item, which stays visible (also when the item had no children or was collapsed). Undo and redo no longer leave the cursor in a hidden block: a zoom that doesn't include it ends, and collapsed items above it expand. Zooming out (the arrow or the path above the blocks) while typing in a sub-item of a collapsed item expands it, so the block you're typing in stays on screen.
- **Phone: a drawer without empty strips** — on Android the drawer (**Menu**) counted the system bars twice: there was an empty strip above the notebook name, and the sync bar at the bottom was too tall and covered the last pages of the tree (e.g. “Recipes”). Now the notebook name is right under the status bar, and the sync bar right above the navigation bar (buttons or the gesture line), so a few more rows fit in the tree.
- **Phone: the drawer shows the open page and a new folder** — the row of the open page in the tree (“Show the open page”), of a new folder and of a page from “Show in tree” sometimes landed under the sync bar at the bottom of the drawer, and tapping it opened Sync. Now the tree scrolls so the whole row is visible — also when the drawer opens after hiding the keyboard or after the window is resized — until you scroll the list yourself.
- **The stylus draws with pressure** — in the drawing editor the stylus only drew like a mouse: the line had the same width everywhere (Brulion's notification layer hid the sheet from the stylus), and on Android a finger after the stylus kept drawing instead of moving the sheet. Now the line gets thicker as you press harder, and the other end of the stylus and its side button erase. See [[Drawings#Stylus, finger and mouse|Stylus, finger and mouse]].
- **Browser version: the other end of the stylus erases** — the eraser on the other end of the stylus drew like a pen in the browser instead of erasing.
- **Drawings: undo after saving and changes from another device** — a change undone or redone (Ctrl+Z, Ctrl+Shift+Z) after the drawing was saved now counts like any new change. Merging with a version from another device (with the drawing open, **Merge drawings** in the Sync center, Nextcloud) and the Excalidraw plugin in Obsidian no longer take it for an older version — before, what was drawn after an undo could disappear, and the undone change could come back.
- **Browser version: the drawing is saved after the last line** — especially in Firefox, anything Brulion does with a delay could stall: the drawing's last line stayed unsaved (saving happens 1.2 s after a change) until something else changed. Now such saves always happen.

## 0.5.0 — October 28, 2026

**New**
- **Drawings in notes** — the `/Drawing` command (on a phone also the **Drawing** button above the keyboard) inserts a drawing and opens it for drawing: a pen with stylus pressure, a highlighter, an eraser (also with the other end of the stylus), colors and widths, plain, lined or grid paper, undo. When you draw with a stylus, fingers only move and zoom the sheet; on a phone without a stylus, your finger draws. A drawing is an SVG file in the attachments: Obsidian shows it as a picture, excalidraw.com opens it for editing, Syncthing syncs it, and changes from two devices are merged line by line. Clicking a drawing in a note opens it again. See [[Drawings|Drawings]].
- **Import from Microsoft To Do** — **Import notes → Microsoft To Do → Sign in to Microsoft**: enter a code at microsoft.com/devicelogin (also on a phone), and Brulion downloads all your lists — each becomes a page with tasks: status, importance, due date, reminder, recurrence, categories as tags, note, steps, links and files. Signing in requires your own free app registration in Microsoft Entra — the guide walks you through it step by step. Without it you can import a JSON file saved from Graph Explorer. See [[Import and export#Microsoft To Do|Microsoft To Do]].
- **Menus on markers and links** — right-click or a long press now also opens a menu on 🔁 (change the rule, ready-made rules, “Don't repeat”), on the priority (from “Highest” to “No priority”), on a link to a day `[[2026-10-20]]` (open, copy, move the date), on a `#tag` and on a `[[link]]` (“Open”, “Open in side panel”, “Copy link”). The reminder menu has **Repeat** and **Priority** submenus. Date menus also work in query results, embedded blocks, table cells, the **Tasks** view and the calendar — the change goes into the note with the task, and you can undo it with the **Undo** button. Only that one marker changes. In the Tasks view a long press on a tag now opens the tag's menu (the tag page is in it, and also under Ctrl+click). See [[Tasks|Tasks]] and [[Links and backlinks|Links and backlinks]].
- **Settings from an Obsidian vault** — an Obsidian folder opened as a notebook automatically takes over the journal folder, file names and template, the templates folder and the attachments folder from Obsidian's settings (`.obsidian/`), as long as the notebook has no settings of its own. **Settings → Notebook** shows what it took over, and **Save to the notebook** writes it to `.brulion/config.json`. See [[Import and export#An Obsidian vault without importing|An Obsidian vault without importing]].
- **Selecting blocks from several journal days** — in the **Journal** view you can select blocks from several days at once: Shift+click on a block of another day, drag with the mouse, Shift+arrows past the edge of a day, or on a phone tap a block of another day. You can copy such a selection (also as text), and each day's blocks go to the clipboard under a link to that day. See [[Document and outline#Selecting and copying|Selecting and copying]].
- **A rename on the server arrives as a rename** — a folder or page that was renamed or moved on another device or in Nextcloud in the browser gets the same new name here. Notes aren't downloaded again and don't go to the trash, and their version history, favorites and collapsed blocks follow them. When you rename a page in Brulion, sync moves it on the server with a single command, just like a folder — also when you keep writing in it right afterwards. See [[Sync#Folders|Folders]].
- **Pictures from the internet: zoom, pasting and export** — clicking a shown picture opens it full-window (fitted or at actual size). For a pasted picture address (“Copy picture address” in a browser) and a fragment of a web page with pictures, Brulion offers to save them in the notebook right away. Export to HTML and Pandoc asks whether to keep the picture addresses or include copies of the pictures. See [[Links and backlinks#Pictures from the internet|Pictures from the internet]].
- **A look made to measure** — a new **Settings → Appearance** section: the accent color (six ready-made ones or your own — Brulion makes sure text on it stays readable), a white background instead of paper and a black background for OLED screens, the interface font, compact density on a computer, and in the **Layout** part — which items the sidebar shows, which four buttons the bottom navigation has on a phone, and whether the quick capture button is visible. In **Settings → Editor**, any installed font for notes and code, and line spacing. Theme, text size and reduced motion moved from the General section to Appearance. See [[Getting started#Appearance|Appearance]].
- **Windows notifications with buttons, and autostart** — on Windows, reminders and alarms are now Windows notifications with **Snooze** and **OK** buttons, and clicking one opens the note. An alarm stays on screen and rings until you respond; you pick the sound from the Windows notification sounds (also **Play sample**). A new switch, **Settings → Application → Start when you sign in** (and a task in the installer), starts Brulion hidden in the tray right after you sign in, so reminders ring from the start of the day. See [[Reminders and alarms#Notifications|Notifications]].
- **Share → Brulion** — on Android, Brulion is in other apps' **Share** menu: you can save a page from the browser, selected text or `.md` and `.txt` files (several at once too) in today's journal or as a new page. Before saving you can edit the text and change the page title; nothing is saved without your consent and nothing overwrites existing pages. See [[Journal, calendar and templates|Journal, calendar and templates]].
- **Android widgets** — on the phone's home screen: **Tasks — My day** (overdue and today's tasks, in a bigger widget also tomorrow's), **Calendar** (the month with dots and an agenda — tasks with a time first) and **Notes** (today's journal, quick capture, favorite and recent pages). Tapping a task opens the note at that line, and **+** adds a task without opening the app: you pick the day, time and alarm with buttons, and the task goes into today's journal right away, even when Brulion is closed. See [[Tasks#Android widgets|Android widgets]].
- **“Done” in the reminder notification** — a reminder on a task has a **Done** button in the notification next to **Snooze** and **OK**: it marks the task as done (`[x]` with a `✅` date; a recurring task gets its next due date) without opening the note. Works on Linux, on Windows, on Android (also on the alarm screen and with the app closed — the change goes into the note the next time the app starts, dated when you tapped it) and on the banner of missed reminders. See [[Reminders and alarms#Notifications|Notifications]].
- **Calendar from the tray icon** — on a computer, Brulion's icon next to the clock opens a small calendar, like the clock in Windows 7 and 10: today's date, the month with dots, and the tasks and reminders of the selected day, plus **Open Brulion** and **Quick capture…**. Clicking a task shows that day in Brulion's window, and clicking outside hides the calendar. On Windows a simple click on the icon opens it, on Linux the **Calendar** item in its menu. See [[Journal, calendar and templates#Calendar from the tray icon|Calendar from the tray icon]].

**Changes**
- **Clicking the tray icon on Windows** now opens the calendar instead of the window. You can bring back the old behavior in *Settings → Application → Click on the tray icon → Window*. A double click always shows the window.
- **The menu of a picture from the internet** now opens with a right-click or a long press (a click enlarges the picture). The menu has a new item, **View picture**.
- **Faster copying of blocks on a Linux computer** — the formatted version of the copy (for mail and document editors) is created only when you paste it into such a program. Copying a whole page with 5000 blocks now takes a few milliseconds instead of about 40, and pasting into Brulion, a code editor or a terminal doesn't create it at all.

**Fixes**
- **Enter at the start of a task's text** inserts an empty task above it, just like in an ordinary list item. The text stays in its block along with its status (a done task doesn't come back as not done) and its sub-items — before, the block was split after the hidden `[ ]`, and the text moved to the new block. See [[Document and outline|Document and outline]].
- **Redo (Ctrl+Shift+Z) doesn't lose an empty paragraph** — after Enter at the start of a paragraph or heading and an undo, redo also brings back the inserted empty block, and the cursor returns to it.
- HTML and Pandoc export: a picture inserted as `![[https://…/cat.jpg]]` was a “missing attachment” — now it's a picture.
- Browser version: a picture from the internet whose site doesn't allow other sites to fetch it (CORS), or one from `http://` in Brulion opened over `https://`, shows that reason instead of “No connection”. Picture requests no longer send headers that made some servers reject them in the browser.
- **The scroll bar doesn't jump** — on long pages, in the journal, in the **Tasks** view, in the calendar agenda, in the version history and in the event log, the scroll bar's handle moves only together with the content, instead of jumping back and forth while scrolling. Its position is counted in blocks (in the journal: in days), so it moves more slowly through a very long day than through a short one, and collapsed blocks don't count.

## 0.4.5 — October 26, 2026

**New**
- **Pictures from the internet** — a picture from an `https://…` address in a note, `![description](https://example.com/cat.jpg)` or with the `/` command → **Picture from the internet…**. Brulion doesn't download anything until you click **Show** (in **Settings → Editor** you can turn on showing them without asking). A downloaded picture works offline too, and **Save to notebook** saves a copy of it in the attachments. See [[Links and backlinks#Pictures from the internet|Pictures from the internet]].
- **Windows version (testing)** — an installer or a portable version (ZIP) for Windows 10 and 11. Reminders are shown by an icon in the system tray.

**Fixes**
- Nextcloud sync on an Android phone and on Windows: when only the letter case of a folder name changed on another device (`projects` → `Projects`), the notes from that folder could end up in the trash. Now the folder is renamed.
- Saving a note that the system briefly refused (e.g. while an antivirus program is checking the file) is retried.

## 0.4.4 — October 25, 2026

**New**
- **Export for Pandoc** — the “Pandoc Markdown (ZIP)” format in the **Import and export** view: a single `.md` document with the whole notebook, a folder or a page, plus the attachments that links lead to. `pandoc "Notebook.md" -o "Notebook.docx"` in the unpacked folder turns it into a Word file (or LibreOffice, PDF or EPUB). `[[…]]` links and block references lead to places in the document, embeds are inserted, and formulas, callouts, tables, tasks and footnotes stay formulas, boxes, tables, checkboxes and footnotes. Dates and reminders are written out in words (“due: 2026-10-20”). See [[Import and export#A document for Pandoc|Import and export]].

## 0.4.2 — October 24, 2026

**New**
- **Changing a date from a menu** — right-click a date marker (📅, ⏳, 🛫, ✅, ➕, ❌) or long-press it with your finger: “Change date…” (a calendar with time), “Today”, “Tomorrow”, “A day later”, “A week later”, “Remove date”. On a ⏰ reminder the menu has “Change date and time…”, “An hour later”, “Tomorrow at this time”, “A week later”, an “Alarm” switch and “Delete reminder”. Only that date changes — the rest of the line stays the same — and you can undo the change with `Ctrl+Z`. An ordinary click on the marker still starts editing. Works in notes and in the journal. See [[Tasks|Tasks]] and [[Reminders and alarms|Reminders and alarms]].

## 0.4.1 — October 24, 2026

**Changes**
- **Removed the “Select text” view** from version 0.4.0 — it didn't work well on phones. To copy, select whole blocks: long-press a block's text on a phone, and on a computer use Shift+arrows, drag with the mouse or Ctrl+click. You can select part of several paragraphs character by character in Markdown source mode (`Ctrl+E` or **More → Markdown source**). See [[Document and outline|Document and outline]].

## 0.4.0 — October 24, 2026

**Note**
- **Tags are now links to pages** (as in Logseq): clicking `#work` opens the page “work”, and its backlinks show the blocks with that tag. The old tag view (subtags, tasks, pages tagged as a whole) is under “Tag overview” in the page header and in the tag list. Renaming a page also fixes the tags that name it. See [[Tags and properties|Tags and properties]].
- **Moving or renaming a page can fix links and tags in other notes**, so that they keep leading to the same files: when two pages have the same name, `[[Home]]` becomes `[[Projects/Home]]`, and `#home` becomes `#Projects/Home`. Query filters (`path:`, `page:`, `tag:`, `ref:`) and Markdown links follow the page too. Before moving, you see how many notes will change.
- **Nextcloud now syncs folders too** — empty folders in both directions, and renaming a folder goes to the server as a single operation. Empty folders left over from renames in older versions will show up once on the other side; delete the ones you don't need, and the deletion syncs too. When the whole notebook lives in a single folder and you rename it and then, before syncing, delete or change a note in it, Brulion will ask you to confirm the deletions. See [[Sync|Sync]].
- **On a computer, dragging the mouse over text selects blocks** instead of scrolling the page — scroll with the wheel or the touchpad (or with your finger on a touch screen, as before).
- **On a phone, the bottom navigation hides while you type**, and the toolbar sits on the keyboard. On Android 11–14 the window now reaches under the system bars, as on 15 and newer.

**New**
- **Moving pages and folders** — “Move to…” in the menu of a page and a folder in the tree (right-click, or “⋯” on a phone), in the **More** menu, in the page menu, in the page list and in the command palette; on a computer also by dragging with the mouse onto a folder or onto the “Pages” header (the notebook's root folder). Links, embeds, references to headings and blocks, tags and query filters pointing at the moved files are fixed, and you can undo the move from the notification. A “/” in the new name moves the page (e.g. `Archive/Home`). See [[Getting started|Getting started]].
- **New folders everywhere** — “New folder” in the “+” menu next to the “Pages” header (also in the root folder), in the command palette and in the move window. A new page goes straight into the folder of the open page; you can change that in the new page window.
- **The folder tree** shows the open page, sorts in Polish order (numbers by value), has the journal folder at the top with readable dates, and remembers expanded folders even after a rename. “All pages” browses folders one by one (“With subfolders”).
- **Copying a link to an item, heading or page** — block menu → “Copy link”: `((id))`, `[[Page#^id]]`, `[[Page#Heading]]` or an embed. You open the block menu by right-clicking any block (also a paragraph or a heading), and on a phone with the “More” button in the bar above the keyboard; `Ctrl+Shift+C` copies `((id))`, and there are commands in the palette too. A clicked link scrolls to the target and highlights it. See [[Links and backlinks|Links and backlinks]].
- **Selecting and copying groups of blocks** — Shift+arrows, dragging with the mouse, Shift+click and Ctrl+click (individual items), and on a phone a long press on a block's text: handles, “One by one” mode and a selection bar. Copy, Cut, Delete, Copy as plain text, indenting and moving whole blocks with their items; text from other apps (HTML) is pasted as Markdown. See [[Document and outline|Document and outline]].
- **“Select text”** — the whole page as one text you can select character by character across paragraphs (on Android with the system handles); in the page menu, under **More** and in the command palette.
- **The toolbar is always at the keyboard** (phone) — in the journal, on a page, in table cells and in source mode; it follows a sliding keyboard. Windows, menus and messages don't hide under the keyboard or cover the line you're writing.
- **A command line for AI agents** — `brulion-cli` reads and writes notes, the journal, tasks, reminders with dates in plain words (“tomorrow 9:00”), queries, tags and backlinks, and moves pages; results in JSON. `brulion-cli mcp` is an MCP server for agents, and once you turn it on in the settings (*Application → For developers*), an agent can also control the running app (`brulion-cli app …`). See [[Command line and AI agents|Command line and AI agents]].

**Changes**
- `#` suggests tags and pages, and the `ref:` filter in queries covers tags too. See [[Queries|Queries]].
- A page's backlinks show the blocks with its tag; a page that doesn't exist yet shows what already leads to it.
- Renaming or moving the folder of the journal, templates, attachments or new pages updates the notebook's settings.
- A line added by another program (e.g. an agent or sync) next to where you're writing merges without a conflict.
- You can rename or move a journal note like a file (in the tree, in the page list, `F2`); the journal view by date doesn't have these commands.

**Fixes**
- The editor doesn't jump to the top of the page when the page header changes height, e.g. after narrowing the window.
- Query results don't overlap the text below them, the “today” marker in calendars moves on at midnight, the reminder window changes language together with the app, and calculation results follow wrapped text.
- Android: the first item of a tall menu doesn't hide under the status bar, and a word still being typed on the keyboard (Gboard) counts in the windows for a new page, folder, quick capture and renaming.
- A deleted or renamed empty folder disappears from the tree right away; names of new folders are cleaned up like page names, and a name that would make the folder hidden is rejected.
- Typing doesn't lose the keyboard when the file changes from outside.
- A reference to a block that contains Markdown shows just the block's text.

## 0.3.7 — October 22, 2026

**New**
- **Changelog** — this page: every change in Brulion since the first version, newest first, with notes when you need to do something yourself. There's a link to it in [[Contents|Contents]].

## 0.3.6 — October 22, 2026

**Note**
- Formulas written since 0.3.3 as `\(…\)` or `\[…\]` now show up as plain text in brackets — change them to `$…$` and `$$…$$`.
- A function call with a comma that could be either a decimal comma or an argument separator (e.g. `round(2,5)`) now gives an error — add a semicolon or use a dot.
- If two notebooks on this device sync with one Nextcloud folder (versions 0.3.1–0.3.5 could have set this up), neither of them syncs, not even after “Sync now”. The sync status at the bottom of the sidebar tells you which notebook they share the folder with; in the Sync center choose “Choose another folder” and give one of them a new folder.
- After “Sign out”, turn Nextcloud sync on in each notebook separately.
- If you opened the documentation in 0.3.0 with Nextcloud turned on, its pages may have ended up in your notebook. You'll find them in the **Tags** view under the tag `dokumentacja` and can safely delete them — see [[Sync|Sync]].

**New**
- **Task status menu** — right-click a task's checkbox or long-press it and choose: To do, In progress, Done, Waiting, Cancelled or No status (the task then becomes an ordinary list item). Works everywhere you see a task checkbox: in a note and the journal, in query results, in embedded blocks, in backlinks, on tag pages, in the calendar and in the Tasks view. A normal click still checks the task off. See [[Tasks|Tasks]].
- **Ctrl+Shift+Enter** opens the same menu in the editor, and on a phone — a long press on the **Task** button in the bar above the keyboard.
- **Status with the `/` command** — “Task: to do”, “Task: in progress”, “Task: done”, “Task: waiting”, “Task: cancelled” and “Task: no status”. Words like `/waiting`, `/deferred`, `/none`, `/todo` or `/done` work too.
- **The documentation updates together with the app** — new pages are added, pages you haven't changed get the new version (the previous one stays in the page's history), and unchanged pages that the new version no longer has go to the trash. Pages you've changed or deleted stay the way you left them. Until now, documentation created by an older version didn't get new pages or fixes — for example the [[Math formulas|Math formulas]] page from 0.3.3.
- **Linux: AppImage updates** — releases have `.zsync` files, so you can update the AppImage (also from earlier versions) with AppImageUpdate, Gear Lever or AppImageLauncher.

**Changes**
- **When a dollar starts a formula** — right before the opening `$` and right after the closing one there can't be a letter or a digit, and a `$` after a space doesn't close anything. Thanks to this, `Netflix 15$, Spotify 10$`, `10$/h` or `$PATH:$HOME` stay plain text and don't swallow links or reminders. An inline formula doesn't continue past an empty line, and a lone `$$` or an unclosed `$` no longer hides the formulas after it. See [[Math formulas|Math formulas]].
- **A backslash and a bracket are just a bracket** — `\(` and `\[` show a bracket again, as in Obsidian and as imports from other programs write them; in 0.3.3–0.3.5 the brackets disappeared and the text between them became a formula. Formulas are `$…$`, `$$…$$` and the `math` block.
- **A formula between `$$` lines** — when each `$$` stands on its own line, all the lines between them belong to one formula, including those starting with `+`, `-`, `>` or `1.` — they don't break up into a list or a quote, also in an outline item. An empty line ends such a formula.
- **What's in a formula stays a formula** — a tag, link, task due date or reminder typed inside a formula no longer goes into tags, backlinks or the calendar, and such a reminder doesn't ring (the editor showed it as part of the formula anyway). A block ID (`^id`, `id::`) and `collapsed::` always work.
- **Dollars in imports** — importing text from HTML, Evernote, Google Keep, Notion databases, Trilium and calendars adds a backslash before `$` where two dollars would form a formula: `Price $10, Netflix 15$` is saved as `Price \$10, Netflix 15$`. Unpaired dollars, code, addresses and property values stay unchanged. See [[Import and export|Import and export]].
- **Decimal comma in functions** — in a call without a semicolon, a comma between digits is a decimal comma whenever that reading is the only one that fits the function: `sqrt(2,25)` gives 1.5 (an error until now). When both readings fit, as in `round(2,5)` or `round(net * 1,23)`, Brulion shows an error instead of guessing (until now `round(2,5)` silently gave 2). See [[Calculations|Calculations]].
- **Picture text is read-only** — “Show as text” shows the file without letting you edit it (until now an accidentally typed character was saved in place of the picture), only the first 200 KB of a large file, and changes to the file on disk, e.g. from sync, show up right away. Ctrl+E switches between the picture and the text both ways, and the **More** menu for a picture has “Show as text” and “Show picture” instead of the note mode switch.
- **Nextcloud: a separate folder for each notebook** — Brulion suggests a server folder separately for each notebook: “Notes” for the first account, and for further notebooks their name (e.g. “Work”, and if that's taken — “Work 2”). You can't save a folder that another notebook on this device already syncs with (nor a folder inside it or above it) — Brulion tells you which notebook that is. Until now every notebook was offered “Brulion”. See [[Sync|Sync]].
- **Nextcloud: “Sign out”** turns off sync in all notebooks on this device, because the app password is shared, and saving a different server or user turns it off in the other notebooks. Until now other notebooks could start sending notes to the new account on their own.

**Fixes**
- **Formula security** — a synced or imported note can no longer insert its own HTML through a formula with `\left<` … `\right>` (a remote image loaded without a click, a script in the HTML export). Brulion's interface doesn't load anything from the network at all; sync works as before.
- **Formula stability** — a very deeply nested formula no longer crashes the app (the part deeper than 48 levels is shown as source), and a note with hundreds of thousands of unclosed `\sqrt[` no longer freezes it for many seconds.
- **Drawing formulas** — `\left\{`, `\big\{` and `\left\|` (sets, piecewise functions), `\bigcup`, `\bigcap` and similar, `\limits`, `\nolimits` and `\displaystyle`, an emoji or `𝔸` in a subscript or superscript, `\&` and braces in matrix cells, and a matrix inside a matrix now come out correctly. `\text{…}` shows `\&`, `\%` and `\$` as the plain characters.
- **HTML export** also shows a `math` block as a formula when it's an outline or journal item (that's how `/Math formula` writes it in an outline); other code blocks in list items come out as code.
- **Import from Trilium** — a formula that would read differently in a paragraph than in Trilium (e.g. because of a `$` in the text next to it) stays code with the LaTeX source, as before 0.3.3, and a word with a hash (e.g. `#urgent`) in the text next to dollars no longer accidentally becomes a tag.
- **Browser: emoji instead of empty squares** — the bells of reminders and alarms (`⏰`, `🔔`), task markers and other emoji are now drawn by Brulion's own fallback font (Noto Emoji and Noto Sans Math, SIL OFL 1.1 license): in one color, the color of the text. It also works for hands with a skin tone from the phone's keyboard and for symbols within text written in Cyrillic or Greek.
- **Linux: emoji and symbols** — without an emoji font, Brulion uses the same fallback fonts, and it draws the block embed symbol ⧉ and rare math symbols with them even next to a color emoji font.
- **Code blocks (a bug from 0.3.5)** — a change in a code block that has a `^id` ID under it (added by “Copy block reference” and `((`), an `id::` property or more text of the same item no longer turns the rest of the page (or the item's children) into code after saving. A `` ``` `` line typed in an unclosed code block closes it again.
- **Code started in a paragraph** — a `` ``` `` line typed in a paragraph or list item and pasted code without a closing line get a closing line, and an opening line on its own (e.g. `` ```js ``) is closed when you move to another block. So code doesn't take the following blocks along with their tasks and reminders. Enter and pasting between these lines work as in a code block. See [[Document and outline|Document and outline]].
- Enter at the end of a task whose text starts with `` ```js `` starts the next task instead of inserting a code block into it.
- A task whose text starts with inline code is shown in full in the Tasks view, and the Logseq import doesn't lose that code.
- **Documentation** — on Android, opening it no longer turns off your notebook's alarms and reminders. After a restart your notebook opens, not the documentation; quick capture goes to your notebook, and tapping a reminder while reading opens the note in your notebook. The documentation also disappears from the list of recent notebooks. See [[Reminders and alarms|Reminders and alarms]].
- Going back to a notebook whose folder has disappeared (deleted, moved, on a disconnected card) with Ctrl+Shift+B or the “Switch notebook” menu no longer creates an empty notebook in its place: Brulion warns that the folder isn't available and removes it from the recent list.
- **Nextcloud: settings from 0.3.0** go to the notebook that was being synced back then. Since 0.3.1 they were taken over by the first notebook opened — even a different one, e.g. when the synced one was on a card that wasn't inserted — and that notebook synced with the old folder. A notebook that lost its settings this way goes back to its own folder when you turn sync on again.
- **Nextcloud: one folder, one sync** — the same notebook folder opened through a different path (e.g. on Android `/sdcard/…` and `/storage/emulated/0/…`) has a single sync setting; until now turning it on through the second path could connect the notebook to a different folder on the server.
- **Pictures** — under an open picture you see the notes that embed it or link to it; the Links tab in the side panel shows them again (since 0.3.4 it was empty for a picture). A small picture isn't stretched when fitted to the view. Formats the device can't read (WebP and AVIF on Android, in the browser and in the NixOS package) are named instead of a loading error — also in a picture embedded in a note. In a narrow view the size button is a magnifying glass icon, so the file name is visible.
- **Clock** — dragging the hand also works when you start from a number (e.g. from the selected value), and touching the center of the dial no longer switches to minutes. On a phone held sideways the dial gets smaller and the hour and minute fields sit next to it, so you see the whole clock without scrolling. The focus outline shows only when you use the keyboard.
- After picking the default reminder time on the clock (*Settings → Notebook*), a time changed on another device shows up right away, and leaving the field no longer saves the old value.
- **Source mode on a phone** — you scroll the text with your finger, a tap places the cursor, and a long press selects a word; until now the text couldn't be scrolled past the first screen.
- Names of folders, notebooks and pages in menus and in the navigation path show up exactly as they're written — a name that looks like HTML tags (e.g. from sync or an import) no longer changes how things look.
- **Browser** — the welcome page of the sample notebook points to the documentation pages in the project's repository, not to the documentation notebook, which doesn't exist in the browser.
- The browser version and the one run with `nix run github:janusz-bit/brulion` show the right version number (after the 0.3.5 release they still showed 0.3.4).
- **Documentation pages** fixed where they didn't match the app — among other things, you set the default reminder time in *Settings → Notebook*, and Ctrl+Enter after “done” goes back to no status. The [[Reminders and alarms|Reminders and alarms]] page explains which notebook does the reminding, and [[Contents|Contents]] how the documentation gets updated.

## 0.3.5 — October 21, 2026

**New**
- **Time on a clock** — you pick the time in the reminder window, in the add window in the calendar and the default reminder time in the settings on a round clock, as on Android: hours 00–11 on the outer ring, 12–23 on the inner one, minutes labeled every five, and you set the exact minute by tapping or dragging around the dial. The clock works with the keyboard too, and you can still type the time by hand.

**Changes**
- **Code blocks** — a `` ``` `` line typed or pasted in a code block stays its content: Brulion lengthens the block's hidden fence by itself. Enter opens a code block also after a line with a longer fence and with any language name, and an embedded code block shows just the code, without the fence lines.

## 0.3.4 — October 20, 2026

**New**
- **Pictures as photos** — clicking an embedded picture or a link to a picture file shows just the photo, fitted to the view. Clicking it (or the “Zoom to actual size” button) shows the natural size, which you move around by dragging; “Fit to view” goes back.

**Changes**
- A picture file is no longer loaded into the editor as text — that produced unreadable characters. That older view opens with “Show as text” in the picture bar or Ctrl+E, and “Show picture” in the page menu goes back to the photo.

## 0.3.3 — October 20, 2026

**New**
- **Math formulas** — Brulion instantly draws LaTeX in a note as a readable formula, without the internet and without running any code: inline between `$…$`, on its own line between `$$…$$` or in a `math` block (also `latex` and `tex`), which you edit in place and insert with the `/Math formula` command. Brulion understands Greek letters, subscripts and superscripts, fractions, roots, matrices, systems of equations (`cases`) and alignment (`aligned`); `\$` is a plain dollar sign. The file keeps the LaTeX source, and HTML export shows the finished formulas. In 0.3.3–0.3.5, `\(…\)` and `\[…\]` were formulas too — since 0.3.6 they're ordinary brackets again.
- **Import from Trilium** brings formulas over as formulas, not as code.
- **Documentation** — a new page, [[Math formulas|Math formulas]], with live examples. Documentation created by an earlier version got it only in 0.3.6.

## 0.3.2 — October 20, 2026

**Fixes**
- The “Switch notebook” menu opens the chosen notebook or the documentation on the first click — until now the first click did nothing.

## 0.3.1 — October 19, 2026

**Changes**
- **Nextcloud in each notebook separately** — turning sync on and the folder on the server now belong to the notebook, while the server and account stay shared. Any other notebook, the documentation included, starts without sync. The existing setting is taken over by the first notebook opened after the update (except the documentation and the sample notebook).

**Fixes**
- Notes no longer move between notebooks. In 0.3.0, Nextcloud sync was shared by all notebooks, so after switching notebooks (e.g. to the documentation) the same server folder synced with the second notebook, and the notes of one ended up in the other.

## 0.3.0 — October 19, 2026

**New**
- **Documentation written in Brulion** — a separate notebook, “Brulion Documentation”: 14 pages with live examples (modes, links, tags, tasks, reminders, calculations, queries, journal and templates, sync, import and export, shortcuts, file format). It's created the first time you open it, and you can edit it however you like. Start with [[Contents|Contents]]. The browser, which has a single notebook, has no documentation.
- **Quick notebook switching** — the “Switch notebook” button next to the notebook name in the sidebar opens a menu with recent notebooks, the documentation and an “Open another notebook…” item.
- **Ctrl+Shift+B** takes you to the documentation and back to the previous notebook; you'll find the same command in the command palette (Ctrl+K).

## 0.2.1 — October 19, 2026

**New**
- **The event log** shows what the app and sync were doing, also during previous runs. You open it from *Settings → Application → Event log*, from *Settings → Sync*, from the Sync center or by typing “log” in the command palette (Ctrl+K). You can filter the entries (e.g. Sync, Network, Notebook, only warnings and errors) and search them, also without Polish letters.
- **The log is easy to send** when something doesn't work. **Copy** works everywhere, on Android there are also **Share** and **Save to Downloads**, on a computer **Save as…** and **Open folder**, and in the browser **Download**. Brulion removes passwords, app passwords, tokens and cookies before anything gets into the log; the log doesn't contain the content of your notes (note names and the server address may appear in it). The log stays on the device until you share it yourself, and in the browser it covers only the current session.
- **Detailed log** (a switch in *Settings → Application*) also records every request to the server. When sync is giving you trouble, turn it on, sync again, and only then send the log.
- **Live sync progress** in the Sync center: the stage (e.g. “Checking files on the server… folder 12 of about 40”), the files being transferred and the elapsed time, and the **Stop** button interrupts the sync.
- **Sync history** — the last 20 runs on this device: when and why they started, how long they took, what they transferred and what errors there were (described in plain words). **Details** shows where the time went, how many requests went to the server and which files couldn't be synced. Runs that are waiting for you (a conflict to merge, a paused mass deletion) are highlighted.

**Changes**
- NixOS: installing from the flake (`nix build`, `nix run`, `programs.brulion`) no longer compiles and runs the tests, so it takes less time.

**Fixes**
- **Sync doesn't run forever** — when the Nextcloud server stops responding, a warning appears after about 20 s, and after 3 minutes without progress the run ends with an error giving the reason. Brulion retries the sync by itself: you see when the next attempt will be, and a **Try now** button.

## 0.2.0 — October 18, 2026

**New**
- **Import from Trilium Notes** — a ZIP export (“This note & all descendants”, in HTML or Markdown format) from Trilium 0.10x, TriliumNext 0.9x and zadam/trilium 0.6x. The note tree becomes pages and folders, day notes go into the journal, labels become tags and properties, relations become links, and pictures and files become attachments. See [[Import and export|Import and export]].
- For developers: you can build the Android APK with Nix: `nix build github:janusz-bit/brulion/v0.2.0#brulion-android`.

**Changes**
- **Faster startup** — with a notebook of 10,000 notes the window appears on a computer after about 0.2 s instead of 0.45 s, and on a slower device after 0.5 s instead of 2.3 s; the journal's content also shows up sooner. In Chromium-based browsers reopening Brulion is roughly a quarter faster, because the browser remembers the app's compiled code.
- **Large notes don't freeze the app** — after startup and after each autosave of a very large note (from about 200 KB), Brulion stopped responding: on a computer for 0.9–1.6 s, on a slower device for as long as a dozen or so seconds. Now it's a fraction of a second at most. Searching large notes is faster too.
- **Faster views and typing** — the Tasks view opens twice as fast, Calendar and Agenda 10–30% faster, and the graph on a slower device is laid out in 2.6 s instead of over 40 s. On very long pages (thousands of blocks), characters appear roughly twice as fast on a slower device. Block suggestions after `((` and page suggestions after `[[` are faster too.
- **Faster import of long lists from HTML** (e.g. from Evernote or Notion) — 20,000 items load in 0.2 s instead of 13 s.
- **Smaller downloads** — the Android APK is smaller (87.1 → 84.9 MB), even though it now includes OpenSSL, and the browser app's file is over half a megabyte smaller.
- **Queries on a touch screen** — a query block first shows 20 results instead of 50 (still 50 when using a mouse); a **Show more** button adds the rest.
- **Tables** — when using a mouse, the **Row** and **Column** buttons under a table appear after you select the table or while you edit a cell, not just on hover, so the table doesn't grow under the cursor while you scroll. On a touch screen the buttons are always visible.

**Fixes**
- **Android: HTTPS and Nextcloud work** — the app was missing OpenSSL, so every HTTPS connection failed with a “TLS initialization failed” error, and the connection test reported an untrusted certificate even with a correctly set up server. Now the APK includes OpenSSL 3, and Brulion also trusts CA certificates you've installed on your phone, so your own Nextcloud server with your own CA works just like on a computer.
- **Scrolling without jumps** — a long journal, large pages and the Tasks view don't stutter, and the content doesn't jump (before, by as much as 1500 px). On Android, a fling starts at the speed of your finger (before, about 0.7 of that speed), and catching a fling in progress doesn't jerk.
- **Android: typed dates and times are accepted** — a date typed on the touch keyboard in the reminder or date window was lost, and a time only counted when a tap elsewhere moved the focus. Now the entry is confirmed by Enter, leaving the field, hiding the keyboard and the **Save** button; the due date and recurrence windows work the same way. The keyboard's action key now says “Done” instead of “Next”.
- **Android: export suggests a file name** — the system save window opened with an empty name, and now it suggests e.g. `Brulion-2026-10-18.zip` in the Downloads folder.
- **Browser: indexing doesn't stop halfway** — the note index paused until you moved the mouse; now it finishes on its own.
- **Import: safe page names** (in all formats) — names reserved on Windows (`CON`, `NUL`…) get a `_`, so such a notebook also syncs with Windows, and invisible characters that change text direction are removed from file names.

## 0.1.1 — October 16, 2026

**New**
- **Reminder and alarm sound** — in *Settings → Reminders* you choose the **Reminder sound** and the **Alarm sound** separately: Default, Ringtone or Silent; **Play sample** lets you listen to it. This setting applies to this device only.
- **Sound on Android** — **Choose ringtone…** opens the system list of ringtones (including the ones you've added yourself); there's also a Vibrate mode and a **System notification settings** button.
- **Sound on Linux** — a sound from the desktop's sound theme or your own `.oga`, `.ogg` or `.wav` file; it's played by the desktop's notification service (e.g. in GNOME or KDE Plasma).
- **Sound in the browser** — Default or Silent; an alarm with the default sound beeps for a few seconds while Brulion's tab is open.
- **Importing notes is easier to find** — **Import notes** is in the **More** menu (⋮) and in the drawer on a phone; a new notebook has an import button under the empty journal, and the welcome screen has a “Notes in another app?” card.
- **Android: import from the phone** — **Choose files…** (a long press selects several files), **Choose a folder…** (e.g. a whole Obsidian vault or Logseq graph) or a typed path. Brulion recognizes the format by itself, from the content of the files, and neither a large folder nor files from Google Drive freeze the app.

**Changes**
- An empty folder in the page tree in the sidebar shows “Empty folder” when expanded.

**Fixes**
- **Android: a tap does what it should** — tapping an item in the drawer, the All pages list, favorites or the Tasks view, or a block's bullet, opened the context menu, so the page, folder or task didn't open. Now the context menu opens with a long press (or the right mouse button).

## 0.1.0 — October 15, 2026

The first release of Brulion — for Android, Linux and the browser. From the start it can do:

- **Two note modes** — an outline with blocks, indenting, collapsing and zooming in (like Logseq) or a document with continuous text and a live preview (like Obsidian). You pick the mode when creating a page and can change it at any time — see [[Document and outline|Document and outline]].
- **A journal** with a page for every day, and templates — see [[Journal, calendar and templates|Journal, calendar and templates]].
- **Links** between pages, block references, backlinks and a graph of connections, plus tags and properties — see [[Links and backlinks|Links and backlinks]] and [[Tags and properties|Tags and properties]].
- **Search** across the content of all notes (also without Polish letters) and quick switching to pages.
- **Tasks** compatible with the Obsidian Tasks plugin: due dates, priorities, recurrence, the Tasks view and a calendar (month, week, agenda) — see [[Tasks|Tasks]].
- **Reminders and alarms** (`⏰` and `⏰!`) — on Android they work even with the app closed and after the phone restarts. You can type dates in words, in Polish or English. See [[Reminders and alarms|Reminders and alarms]].
- **Calculations** in `calc` blocks (variables, percentages, units, currencies), tables with spreadsheet-like formulas and sums inside text — see [[Calculations|Calculations]].
- **Queries** — a `query` block gathers tasks and blocks from the whole notebook as you go; see [[Queries|Queries]].
- **Sync** through Syncthing (a plain folder, merging conflict copies) or Nextcloud/WebDAV (in the background, three-way merge, protection against mass deletion) — see [[Sync|Sync]].
- **Version history and trash** — compare and restore previous versions of a note, and recover deleted pages.
- **Import** from Obsidian, Logseq, Notion, Evernote, Google Keep, Joplin, Simplenote, Roam, Bear, Markdown and TXT files, HTML and iCalendar, and **export** to 9 formats: ZIP with Markdown, Obsidian layout, Logseq layout, HTML website, a single HTML file, JSON, iCalendar, OPML and a CSV of tasks — see [[Import and export|Import and export]].
- A command palette (Ctrl+K), quick capture to the journal, a toolbar above the keyboard on phones and a “Sample notebook” with notes to try things out.
- The interface in Polish (English available), light and dark themes.
- **Platforms** — Android 9 or newer (arm64 APK), Linux (AppImage for x86_64 and aarch64, a NixOS package) and the browser, also installed as an app and offline.
