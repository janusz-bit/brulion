# To do

A list of ideas and known gaps, split into categories. The order means nothing. An item that gets
done in the repository simply disappears from the list.

## Features

* **AI support** — scope to be decided (today AI agents use `brulion-cli`, the MCP server and app
  remote control: docs/CLI.md). In other apps: an assistant in the editor with your own API key
  (AFFiNE), a writing assistant (SiYuan); also worth considering: questions to the notes and
  semantic search on a local model.
* **Extensibility as in Emacs and TiddlyWiki** — the user changes and extends Brulion from within
  the notebook (custom commands, views, macros, behaviours); scope and security to be decided
  (notes arrive from sync and import).
* **A small language for the interface layout (in the style of Typst)** — a text file describing
  what the icon bar, the pages panel, the "More" menu, tabs and the bottom navigation show and in
  what order (icons, labels, groups). Today this is set only in Settings → Appearance → Layout
  (hidden items, order of the panel's sections, bottom navigation buttons).
* **Notebook settings in notes** — the notebook settings stored as a note (a page in the notebook,
  editable like any other and synced together with it) instead of `.brulion/config.json`; to be
  decided: the format, moving the existing settings, and what stays a device setting.
* **Pictures from the internet — check on a real phone.** The Android emulator
  (`scripts/android-emulator-test.sh --web-pictures`) and the Chromium and Firefox browsers
  (`tests/web`, GitHub Actions) check this automatically; a real phone remains
  (docs/ANDROID.md "Pictures from the internet"): panning a zoomed picture with a finger and the Back
  gesture from the edge, pasting an address copied in Chrome via "Paste" from the text menu, export
  with "Include copies".
* **Drawings — further ideas and checks on devices.** The drawing editor (`/Drawing`, docs/UI.md
  "Drawing editor") has handwriting, a highlighter, an eraser, paper, shapes, arrows, text, lasso
  and rectangle selection, resize and rotation handles, selection style, flips, groups, backdrops
  (picture, slide, PDF page) with moving to neighbouring PDF pages, a dark background and a custom
  background colour, alignment and distribution, labels in polygons, a clipboard compatible with
  excalidraw.com, pictures from "Embedded Files" of Obsidian drawings and their export as pictures.
  What remains:
  * check "Merge drawings" by hand on real devices: a drawing changed offline on the phone and on
    the laptop, Syncthing leaves a `*.sync-conflict-….svg` copy (or `*.excalidraw.sync-conflict-….md`
    for an Obsidian drawing) → "Merge drawings" in the sync center on one of them; the same with a
    real Nextcloud (a drawing changed on two devices merges by itself, without a copy). Copies
    from a real Syncthing (two instances on one computer) are merged by
    `tests/app/io/syncthing-conflict.sh`; Nextcloud was checked only on a fake WebDAV server
    (`tst_sync_devices`);
  * on a real tablet with a stylus (Android, Natalia — in the app and in the browser) and a phone:
    whether the line gets thicker with stronger pressure, whether the other end of the stylus and
    the side button erase, lasso selection with the stylus, Polish characters from the on-screen
    keyboard in text on a drawing; insert pages of a lecture PDF ("/Drawing on a picture or PDF…";
    a PDF picked in the system file chooser from the notebook folder should stay where it is,
    without a copy in the attachments), write with the stylus on a slide, check the dark backdrop
    and whether a rotated PDF page looks right (checked automatically: Chromium and Firefox —
    `tests/web`, the Android emulator — `--stylus`, `--drawing`, a PDF page through PdfRenderer —
    the bridge self-test, compatibility with excalidraw.com both ways —
    `tests/app/drawing/excalidraw-roundtrip.sh`, needs the internet);
  * check by hand the resize and rotation handles and the "Style" panel with a stylus and a finger
    on a real tablet and phone;
  * check by hand in the real excalidraw.com (needs the internet): pasting elements copied in
    Brulion (`Ctrl+C`) and the other way round, and a label in a triangle or a star saved by Brulion
    (text grouped with the shape) — whether it stands in the same place and moves together with the
    shape; in Obsidian: a picture from "Embedded Files" still loads after the drawing was edited in
    Brulion;
  * check by hand Obsidian drawings on a real vault (the tests rely on files written according to
    the plugin's code; Brulion reads and writes 22 files from public vaults correctly, but they
    were not opened in Obsidian afterwards): open a `*.excalidraw.md` in Brulion, draw something
    and move a text with a `[[…]]` link, open it in Obsidian (the drawing loads, the link in the
    text works, the notes on the back are untouched); erase a stroke in Brulion while Obsidian has
    that drawing open (it should disappear there too); open in Obsidian a drawing on a slide (the
    backdrop is visible under the writing).
* **Sharing outside Android — check on real devices.** Checked automatically (tests, CI): reading
  a share, the window, handing over to a running instance, the `.desktop` entry, the Windows
  registry (installation, update, uninstallation), the web version in headless Chromium
  (`wasm-smoke.mjs --share`). Left to do by hand: Chrome or Edge on Android (the web version
  installed as an app → Share → Brulion from a page and from selected text, with Brulion closed
  and open); Windows Explorer ("Open with → Brulion" on `.md` and `.txt`, several files at once,
  the window on top, the default program unchanged); the file manager on Linux (GNOME Files,
  Dolphin: "Open With → Other Application", the window comes to the front under Wayland,
  double-clicking a `.md` still opens the previous program; Cinnamon/Xfce under X11: the cursor
  does not keep spinning after opening a file in a running Brulion); an AppImage integrated through
  Gear Lever or AppImageLauncher.
* **PDF viewer — check on devices and further ideas.** PDF thumbnails in notes and the PDF viewer
  (docs/UI.md "PDF viewer") are checked automatically: tests with Qt PDF and poppler, the shell
  self-test (`tst_shell_qml`), the Android emulator (`--pdf`: pinching, searching on API 36, no
  search on API 30, "Open in another app", "Draw on this page", memory), Chromium and Firefox
  (`tests/web/pdf.spec.mjs`: a new tab; Playwright's Firefox has pdf.js turned off, so the
  download is checked there). What remains:
  * by hand on a real phone and tablet: smoothness of pinching and scrolling, the Back gesture from
    the edge, search on Android 15+, "Open in another app" with real PDF apps (Android, the Windows
    default app, GNOME and KDE), Safari on iOS (a new tab or a download), ordinary Firefox with
    pdf.js (a new tab), large lecture scans (100+ MB) — memory on the phone (docs/PERFORMANCE.md,
    L19);
  * by hand on a real phone, tablet and computer: text selection (dragging with the mouse, holding
    a finger and moving), the table of contents, links, entering a password, two pages side by
    side, annotations from real PDFs (highlights, notes), "Open in another app" for DOCX / XLSX /
    MP3 with real apps;
  * on Android 15+ (API 35) connect text selection (`selectContent`), links (`getLinkContents`,
    `getGotoLinks`) and opening password-protected PDFs (`LoadParams.setPassword`) to
    PdfRenderer — needs checking on an emulator or a phone; PdfRenderer does not give the table of
    contents at all;
  * the PDF table of contents in the builds with Qt PDF (Windows, AppImage): `QPdfBookmarkModel`
    does not guard against a looping outline (recursion until stack overflow — `tst_pdfpages`
    `loopingOutline`), so today the table of contents comes only with poppler; a safe bookmark
    reader of our own is needed;
  * creating your own annotations (highlights, notes) saved in the PDF — today existing ones are
    only shown, new ones are made in another app.
* **Spell checking — check on devices and further ideas.** Underlining errors in the edited
  block, suggestions, the user dictionary and "Ignore" (docs/UI.md "Spell checking") are checked
  automatically on a computer (Hunspell, tests and the editor self-test). What remains:
  * on a real Android phone: the system spell checker (Gboard, Polish and English) — underlines,
    tapping a word and suggestions, spell checking turned off in Android's settings (Brulion's
    spelling settings disappear); the Kotlin/JNI code (`SpellCheck.kt`) has not run on a device;
  * AppImage and Windows: build in CI and check that the dictionaries from the package load
    (Windows: a program folder with Polish characters in its path);
  * a user dictionary synced between devices (today a device setting — Nextcloud carries only
    `config.json` from `.brulion/`, and merging by keys would lose words);
  * underlines also outside the edited block and in the Markdown source mode.
* **Vim mode** — optional movement and editing with Vim keys in the block editor and in source
  mode (Obsidian); command shortcuts can already be changed in Settings → Keyboard shortcuts.
* **Redesign the notebook choice screen** (`OnboardingView`, "Welcome to Brulion") — on a wide
  desktop window it is a narrow column in the middle with empty space on both sides and has to be
  scrolled; the three feature tiles wrap into cramped lines of small text; the cards' descriptions
  are smaller than the rest of the text; "Import notes", "Open from GitHub" and "Try the sample
  notebook" are three similar stacked cards; the "Open notebook" button barely reads as the main
  action; "Recent notebooks", the most useful part for someone coming back, is last and cut off
  below the fold (the header says 7, four are visible). The list also holds temporary notebooks of
  test runs (`/tmp/…/scratchpad/vault`): such runs should use their own profile, and a recent
  folder that no longer exists should be marked or dropped.

## Views and data

* **Database views from properties** — a block or a file showing pages as an editable table, a
  kanban board (also tasks by status), a gallery, a calendar; filters, groups, columns, formulas.
  Obsidian has Bases (table, list, map, kanban), Logseq a table and a gallery, Anytype and AFFiNE
  boards and galleries. `query` blocks today show only a list and do not filter by page properties
  (`status:: in progress`, `cost > 100`).
* **Obsidian compatibility: `.base`, `.canvas`** — `.base` and `.canvas` files from a shared vault
  are today a file in Brulion (docs/IMPORT.md); to be decided what to read and what to edit as
  well. Dataview's `CALENDAR` views, inline `= …` queries and `dataviewjs` stay code
  (docs/SYNTAX.md §10 "Dataview queries").
* **Whiteboard (infinite canvas)** — notes, pictures, PDFs and drawings laid out on a canvas, with
  connections; preferably in the JSON Canvas format (Obsidian Canvas, AFFiNE). The drawing editor
  covers sketches, but does not embed notes in them.
* **Flashcards with spaced repetition** — cards from blocks (`question :: answer`, hidden parts),
  spaced repetition (e.g. FSRS), cards due for review today, state saved in files and merged by
  sync (Logseq, SiYuan).

## Tasks and calendar

* **CalDAV calendars** — two-way sync of tasks and events (Nextcloud, Google, Thunderbird, DAVx⁵)
  and read-only subscription to ICS calendars in the calendar view. Today there is a one-time
  iCalendar import and export.
* **Reminders and alarms stay chips while a block is edited** — today a focused block shows its
  raw Markdown, so `⏰ 2026-11-20 18:00` and the alarm turn into plain text as soon as the note is
  entered (docs/UI.md: only an unfocused block shows the rendered chips). They should stay amber
  chips in the editor too, with their menu (change the time, snooze, remove) and the caret moving
  over them as one unit, while the file keeps the same text.

## Capture

* **Web Clipper** — a Chrome and Firefox extension saving an article, a selection or a screenshot
  as Markdown with pictures, with a choice of place and a template (Obsidian, Joplin). Today:
  "Share" with the title and address, without the article's content.
* **Voice notes** — a recording in the app as an attachment and on-device transcription, also in
  Polish, without sending audio to the cloud (Joplin, Obsidian for iOS in preparation).
* **Audio and video in notes** — a player for `![[recording.mp3]]` and `![[video.mp4]]`; today such
  files open only read-only as text.
* **Photo and scan from the camera** — `/Photo` on the phone, straightening and cropping a
  document, scanning to PDF.
* **On-device automation** — `brulion://` addresses (open a page, append under a heading), intents
  for Tasker on Android, a quick settings tile (Obsidian: URI, Siri and Shortcuts).
* **More import sources** — Apple Notes, OneNote (Obsidian Importer), Todoist, Airtable and CSV as
  pages with properties (Obsidian).

## Search

* **Searching attachments and OCR** — text of PDFs and pictures in the whole-notebook search, in
  the background and offline, with a jump to the hit on the page (Joplin, SiYuan). Today a PDF is
  searched only after it is opened.

## Privacy and security

* **End-to-end encryption of sync** — files on the Nextcloud or WebDAV server unreadable without
  the user's password (Joplin, SiYuan, Logseq, Anytype, Obsidian Sync); to be decided: fitting the
  "plain files" principle, the key on devices, recovery and merging conflicts.
* **App lock** — PIN or biometrics on the phone, hiding the content on the recent apps screen and
  in widgets.
* **Encrypted notes** — single pages or fragments behind a password, compatible with sync
  (Trilium: protected notes).
* **Backups: next steps** — restoring a ZIP that is not on the list and files over 1 GB (Vault
  keeps the file being written in memory), an Android folder chosen through a cloud document,
  splitting the compression of a large file into chunks in the browser (today a file over 4 MB is
  saved uncompressed).

## Sharing and collaboration

* **Publishing** — a page or selected notes at an address, optionally with a password, with a
  single command: GitHub Pages, your own server or a public Nextcloud link (Obsidian Publish,
  Logseq Publish, Joplin). Today there is a static HTML export, which is published by hand.
* **Shared notebooks** — a notebook shared with another person: who changed what, comments; to be
  decided whether Nextcloud is enough or live editing is needed (Logseq RTC, AFFiNE, Obsidian:
  "Multiplayer" planned).
* **Notebooks from GitHub — check against the real GitHub and further ideas.** The GitHub notebook
  (docs/UI.md "Notebooks from GitHub", core `github`) is tested against an in-memory GitHub
  (`tests/core/github/fakegithub.h`). What remains:
  * by hand against github.com, on the desktop, Android and in the browser (the site and the
    single HTML file): a fork made a moment ago taking the new branch and the pull request at once,
    text sent inline in a tree coming back with the same blob id (CRLF, Polish letters), the
    anonymous rate limit with the raw.githubusercontent.com downloads, a fine-grained token, a
    repository with thousands of files and a folder deep inside it, an empty repository's first
    commit, the CORS answers of api.github.com with a token;
  * other Git hosts (GitLab, Codeberg/Forgejo, a server of one's own) and plain Git: their own
    REST APIs, or a Git library on the desktop and Android (no git in the browser);
  * a sign-in without pasting a token (GitHub's device flow needs a registered OAuth app and is
    refused by browsers' CORS), automatic commits after edits (GitJournal, Obsidian Git), the
    history of a note from the repository's commits.

## Platforms and distribution

* **iOS, iPadOS and macOS** — today only the browser version; Obsidian, Joplin, Anytype and others
  have apps there. On iOS alarms, access to the notebook folder and Syncthing would need solving.
* **Stores and repositories** — Flathub, winget, AUR, Homebrew. Google Play restricts all files
  access, which the Syncthing folder needs (docs/ANDROID.md).
* **More interface languages** — translations beyond Polish and English, and with them natural
  language dates, the calculator and the introduction (Obsidian: many languages, also at the
  start).
* **Themes** — theme files exchanged between users (colours, fonts, spacing), beyond the appearance
  settings (Obsidian, Joplin); no code from notes.

## Windows

* **Signed files** — the installer and programs signed with a certificate, without the SmartScreen
  warning; then remove the "(test version)" label (docs/WINDOWS.md).
* **Feedback on the desktop version** — collect it from the tester (who will look at it later).

## Performance

Details in docs/PERFORMANCE.md, "Next steps":
* **Tabs prepared in the background** — the bottom navigation's views (Journal, Tasks, Calendar,
  also All pages and Search) are kept once shown (`ShellRouteView`, `KeptLoader`), but each is
  built only on its first visit, which takes 0.5–1 s on a phone (docs/PERFORMANCE.md "Switching
  views"). After the notebook opens they should be built in the background, one at a time while
  the app is idle (asynchronous `Loader`, hidden), so that even the first switch shows the view at
  once; to measure: start-up time and memory on a phone and a Raspberry Pi;
* faster merging of ready notes into the index on the UI thread, less view refreshing during
  loading (in the browser the first load without a ready index takes 11–16 % longer than before
  wave 5) and measuring indexing on a Raspberry Pi and a phone (L1b);
* measuring the refresh of lists after saving (tasks, calendar, reminders, queries) on a Raspberry
  Pi (before the change 0.95 s, target < 0.2 s) and lowering its baseline row in
  tests/bench/baselines/pi.csv (L3b);
* measuring the faster undo on a Raspberry Pi (before the change 214 ms, target < 50 ms) and
  recording its baseline row in tests/bench/baselines/pi.csv — until then the performance check
  on the Pi skips it (L5b);
* measuring the QML code compiled for size (`-Oz`) on a real phone against a build with
  `-DBRULION_QML_CODE_FOR_SIZE=OFF`: journal scrolling, opening a long page, the views — on the
  desktop no slowdown was measurable (L20b);
* developer QML and self-tests (`qml/dev`, `qml/content/*SelfTest.qml`) outside `src/app`, so that
  releases do not package QtTest, QuickTest and Qt.labs.folderlistmodel (L9b: APK −0.84 MB, web
  version −0.27 MB before wasm-opt);
* **File dialogs — check by hand**: Windows (Open/Save in import, export, "Image…" and the event
  log), AppImage in a real GNOME and KDE session (the portal dialog; without a desktop session
  checked only with xdg-desktop-portal-gtk under Xvfb), Android with Google Drive (import and
  "Save…"), the browser on a phone (several files at once);
* **Graph — check by hand and measure on a phone** (layout on a worker thread and drawing through
  the graphics card done, L16b): on a real phone with a large notebook panning, zooming and
  dragging pages while the layout runs; Windows with a graphics card (Direct3D 11) and the browser
  on a phone; measuring on a Raspberry Pi (software rendering) and in the Android emulator.
