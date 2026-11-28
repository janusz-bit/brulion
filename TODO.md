# To do

A list of ideas and known gaps, split into categories. The order means nothing. An item that gets
done in the repository simply disappears from the list.

## Features

* **Code mode: what is still missing** — projects, AI agents' threads in a project's folder or a
  worktree of their own, the Changes panel with turn checkpoints, commits, pushes and pull requests,
  the terminal and the project's files — picker, viewer, search, @-mentions — are there
  (docs/CODE.md; tried with a real Claude Code and Codex: tst_shell_agents realAgentThread; its tests
  run on Windows too: the ConPTY terminal, git, gh). Missing: Code mode tried by hand on Windows and on
  macOS at all; t3.json read again when it changes, on Windows (CI's runner saw no change of it:
  tst_shell_codeprojects configInProject skips it there); a terminal on Windows before 10 1809;
  staging hunks of the Changes panel's other sources than Uncommitted (they are reverted alone; single lines are staged or thrown away),
  the thread's conversation in the AI's question for a pull request (docs/CODE-GIT.md "Not done (yet)"); for a thread of another computer's project (continued from
  the phone through `acp serve --projects`): committing, pushing, pull requests, reverting a turn and
  the terminal from the phone, a worktree from another base, projects of two computers with the same
  id; T3 Code's in-app browser for `previewUrl` (the
  system's opens it),
  multi-repository projects and custom worktree layouts; T3 Code's branch naming modes (the AI names
  a new worktree's branch: a semantic prefix it picks, the user's own instructions); worktrees cleaned
  up by their age alone, whatever their threads (a settled thread's is, after its merge or some days:
  docs/CODE.md "Settling and snoozing"); a new thread's title written
  by the AI by itself ("Regenerate title" is there); in the file
  viewer (docs/CODE-FILES.md): rendered HTML / PDF files, the index following the disk on Windows (Linux and macOS watch the folders);
  in a thread's composer:
  mentions of threads, models and pull requests (`#`), review comments as chips instead of text.

* **AI: what is still missing** — extensions can ask a model (`b.ai` with tools and pictures,
  Claude, any OpenAI-compatible API, a server of one's own, or a model on the device itself) and
  the built-in AI assistant is an agent over the whole notebook — full-text search, queries,
  reading, pictures, changes the user confirms (docs/EXTENSIONS.md "AI"); missing: a try of the
  remote MCP server (`brulion-cli mcp serve`, docs/CLI.md "Remote MCP server") from claude.ai and
  ChatGPT themselves (it passes the official MCP Python SDK and reads their published client
  documents) — also as the desktop app runs it (Settings → Extensions → claude.ai and ChatGPT) —.
* **T3 Code's features in the AI chat: what is still missing** — the queue and Send now, Edit from
  here, forks, quotes, @-named pages, drafts, approvals, plans to approve, a turn's changed files
  with Undo, delegation between partners, scheduled conversations and their webhooks, a secret the
  agent asks for in a private card, subscription limits, the usage history with API-equivalent costs
  from the agents' own session logs, dictation on the desktop, prompt links, drawings in answers,
  tool calls in words, the list's pins, archive, search, badges and notifications, side chats
  (`/btw`), sub-conversations that wake their parent, conversations archived by themselves, a turn's
  work log ("Worked for …") and find in a thread are there (docs/UI.md "AI chat", "AI agents
  (ACP)", "Agents delegate"). Tried with a real Claude Code and Codex: a thread's turns
  (tst_shell_agents realAgentThread), a sub-conversation that wakes its parent, either way between
  the two (realAgentWakesParent), the queue, steer, a side chat, a fork, Edit from here and a turn's
  changed files with Undo — its editing tool and a shell command (realAgentChatFeatures,
  realAgentUndoesItsChanges, 2026-11-28), a plan to approve in Claude Code's plan mode
  (realAgentPlanApproved), a secret Claude Code asks for in a private card and reads from its file
  (realAgentAsksForASecret), a scheduled conversation and a webhook's run
  (realAgentScheduledAndHooked), the usage read from Claude Code's own session logs
  (realAgentUsageCounted). Not yet with them: the subscription limits (read over the network with
  the agent's own sign-in token: not something an automatic test should do on its own).
* **AI agents (ACP): what is still missing** — an agent the user runs answers in the AI chat over
  the Agent Client Protocol (docs/UI.md "AI agents (ACP)"; tried with Claude Code, Codex and
  OpenCode), on the computer or, through `brulion-cli acp serve` on a computer of the user's — the
  desktop app's "Share agents with the phone", found in the notebook and paired without a code in a
  tailnet —, on the phone and in the browser (docs/CLI.md "AI agents for other devices",
  services.brulion-acp). Missing: a try from a real phone in a real tailnet (the offer in the
  notebook, the pairing by tailnet identity, the firewall's port; Tailscale Serve's header only
  against the fake tailscaled), tailscaled's LocalAPI on Windows (its named pipe) and macOS (the
  `tailscale` program answers there: untried), a try with a real agent from a real phone (its
  foreground service with the screen off included; a question that comes while the phone is in a
  pocket only changes that quiet notification's text — it could alert), a try with
  Gemini CLI (it needs a Google sign-in) and on Windows (a preset's `npx.cmd` through `cmd.exe
  /c`) and macOS; and a try of the other direction, Brulion's assistant as an ACP agent
  (`brulion-cli acp agent`, docs/CLI.md "The assistant in Zed and JetBrains"), in a real Zed and a
  JetBrains IDE.
* **AI agents on another computer changing the app live: on a real phone** — the tunnel is there
  (`acp serve` hands the session's `brulion_app_*` calls to the device's stream, the app answers
  them in process: docs/CLI.md "The app's live tools"), tested between two processes of a desktop;
  untried with Android's app (the phone asleep or in the background while a call waits, a
  screenshot of its window over a mobile network) and with `services.brulion-acp`.
* **The AI chat and the writing assistant: what is still to check** — on a real phone: a photo
  from the phone's camera app for a message (the emulator's camera and the app's process ended
  while it is in front are checked: `scripts/android-emulator-test.sh --camera`) and dictation with
  Google's recognizer and with none (GrapheneOS: docs/ANDROID.md "Camera and dictation"); Claude's
  summarised thinking with a real key (`thoughts = true`, `ai::showsThinking`); the desktop's
  dictation (`media::Dictation`) with a real microphone on Windows; the browser's picture picker
  for a message in a real browser (CI's headless Chrome cannot open a file picker); the chat's
  notifications (an answer, a failure, a question while the app is behind another, an AI agent's
  own — `brulion_app_chat_notify`: docs/UI.md "AI chat") on a real phone — Android shows them as a
  timer's end (`AndroidNotifier::showMessage`), untried — and in a real browser; and a side chat's
  pane (`/btw`) on a real phone (in place of the conversation, Back closing it).
* **The model on the device: what is still missing** — LFM3-VL 2B, LFM3 1.7B and Brulion
  Assistant 1.7B (LFM3 1.7B with the adapter trained for the assistant: docs/FINETUNE.md) run in
  the app through llama.cpp on Linux (the Nix package, the AppImage), Windows and Android
  (docs/EXTENSIONS.md "AI", docs/ARCHITECTURE.md "The model on the device"). Missing:
  * a try on real Windows hardware: CI loads the plugins from the packages without a model; a
    model answering there (MSVC's processor plugins, their speed against the Linux build's Clang),
    a transcript and search by meaning, and a user folder with Polish letters in its path;
  * measurements on a mid-range phone with other cores (the Pixel 9 Pro is measured:
    docs/PERFORMANCE.md "The model on the device"), of the system text and tools read back from
    the disk on a phone, and an Adreno GPU through llama.cpp's OpenCL backend (Mali's Vulkan was
    measured: it writes as fast as the processor and reads four times slower — not worth its
    35 MB);
  * a hot phone (one that charges heats up within minutes of answering): Android caps the big
    core below the middle ones (the Pixel's X4 at 0.7–1.4 GHz against the A720s' 1.8) and the
    model writes at a third of its speed; leaving the capped core out was no clear gain in one
    measurement (docs/PERFORMANCE.md "The model on the device");
  * Android with the foreground service (a model answering, downloaded or copied: docs/ANDROID.md
    "A model on the phone"): not run on a phone yet — how fast an answer is written behind another
    app and with the screen off, a running service switching between dataSync and specialUse, the
    copy of a picked model file of one's own from a document provider, and whether Google Play
    accepts specialUse for it (docs/GOOGLE_PLAY.md);
  * models of one's own measured with the assistant (`brulion-assistantbench --model-id own
    --as-app` with Qwen3 4B, Gemma 3, Llama 3.2 files: Qwen3 0.6B calls a tool right but did 0–1 of
    six tasks), and the smaller tool set (off by default: 28 against 32 of 39 tasks on LFM3 1.7B,
    one attempt each — docs/PERFORMANCE.md) measured with more attempts and the shorter results
    apart from it;
  * stores: F-Droid's and Google Play's rules for an app that downloads model weights under a
    licence that is not free software (a store build without it: `BRULION_LOCAL_AI=OFF`).
* **Extensibility as in Emacs and TiddlyWiki: what is still missing** — extension pages, approval
  per device (of the version read), live reload, hooks (`save`, `change`, `task`, `journal`,
  `open`, `day`, `timer`), shared code, settings, timers, copies between notebooks, the type check
  on save, the Luau console and its syntax colours, macros in the text (`{{ext:…}}`, computed
  wherever a note's text shows), template variables, ```` ```view ```` blocks, side panel
  sections and the views' input controls are there (docs/EXTENSIONS.md). What remains: checks on
  real devices — a view block in a page drawn by `PageItem` (`BRULION_PAGE_ITEM=1`) and the new
  view controls (date dialog, slider, text area) on a phone,
  the type checker on an Android phone (built in, measured, not run on one yet), the browser's
  hostile-extension self-test in Safari (Chromium's and Firefox's windows pass it: docs/WEB.md).
* **The app as code (`b.app`, live code): what is next** — the app's settings, theme tokens and
  eleven slots of the UI are scriptable, its built-in views replaceable, and agents change the running app live (docs/EXTENSIONS.md
  "The app (`b.app`)", "Live code"). Still open: the bottom navigation's and the keyboard toolbar's
  extension buttons checked on a real phone (the self-test's phone run has them).
* **Gym: next steps** — the exercises' pictures checked on a real phone; the rest timer's notification with the
  app closed checked on a real phone (the emulator's test passes) and on a desktop that quit (nothing can show it there: a scheduled notification of the system?).
* **A small language for the interface layout: what is still missing** — the icon bar's order and
  groups, the pages panel's sections and the bottom bar are written as text in Settings → Appearance →
  Layout → "Edit as text…", and a notebook's own in its settings note (docs/NOTEBOOK-SETTINGS.md "The
  layout language"); missing: the "More" menu's contents and the tabs as places.

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
  * the PDF table of contents of the builds with Qt PDF (Windows, AppImage), now read by core
    `pdfoutline` from the file: checked by its tests and on CI's Qt PDF builds, not yet by hand
    with real PDFs there (an encrypted PDF has none);
  * creating your own annotations (highlights, notes) saved in the PDF — today existing ones are
    only shown, new ones are made in another app.
* **Spell checking — check on devices and further ideas.** Underlining errors in the edited
  block, in source mode and in the rendered blocks, suggestions, the user dictionary and "Ignore"
  (docs/UI.md "Spell checking") are checked automatically on a computer (Hunspell, tests and the
  editor self-test). What remains:
  * on a real Android phone: the system spell checker (Gboard, Polish and English) — underlines,
    tapping a word and suggestions, spell checking turned off in Android's settings (Brulion's
    spelling settings disappear); the Kotlin/JNI code (`SpellCheck.kt`) has not run on a device;
  * AppImage and Windows: build in CI and check that the dictionaries from the package load
    (Windows: a program folder with Polish characters in its path);
  * a user dictionary synced between devices (today a device setting — Nextcloud carries only
    `config.json` from `.brulion/`, and merging by keys would lose words);
  * the underlines outside the edited block and in source mode (docs/UI.md "Spell checking") on
    a real Android phone with "Underline in the whole page" on (every word on screen to the system's
    checker: how long it takes, battery) and a source-mode page of 0.5 MB in a Release build on
    the phone (the slices were measured on a computer's Debug build only);
  * the rendered blocks underline no word inside a Markdown link's text (the traced rendering keeps
    a link as a whole) and nothing in transclusions, query results or the RenderedTextItem.
* **Vim mode: what Vim has beyond it** (docs/UI.md "Vim mode") — block visual mode (Ctrl+V:
  the app's paste today), patterns in `/` `?` `:s` `:g` (plain text today: a safe pattern engine
  first), the jump list (Ctrl+O / Ctrl+I: the app's keys today), `is` / `as` growing a visual
  selection; the macros, Ctrl+A / Ctrl+X, sentences, H M L and `:g` / `:normal` are tested on the
  engine, over blocks and over source text only — checked with a hardware keyboard on an Android
  phone and in the browser build (offscreen and the shell self-test only so far; H M L read the
  view of BlockEditor and SourceEditor, which only a real window shows).
* **Start screen on real devices** — the redesigned start screen (docs/UI.md "The start screen")
  was checked offscreen only: on an Android phone, a recent notebook on shared storage without
  "All files access" (shown as available, opening it asks) and the rows' × as touch targets; in
  the browser build, the one notebook without the folder field.

## Views and data

* **Database views: what remains** — dragging cards on a board and a calendar was tested with the
  mouse and the offscreen self-test, not yet with a finger on a real phone nor in the browser
  build; Bases' map view (Obsidian's Maps plugin) is a table; a database view's table has keys
  between cells only while one is edited (Tab, the arrows: docs/UI.md "Database views") — no cell
  cursor without editing, no multi-cell selection, copy or paste there (a paste into several rows
  would be several edits, some of other files, which one toast's Undo cannot take back); a
  Markdown table's keys between cells and its cell selection (docs/UI.md "Special block
  delegates") were checked in the offscreen self-test only — with a hardware keyboard on a real
  computer, a tablet and in the browser build — and touch UIs have no range selection, only the
  cell toolbar's "Next cell".
* **Obsidian compatibility: what remains** — canvases keep the Advanced Canvas plugin's portals
  in the file but draw plain cards, and its shapes other than a pill or a circle and its dashed or
  dotted borders are rectangles on the board (the canvas's picture, `canvas::toSvg`, draws them); Dataview's `dataviewjs` blocks and inline
  `$= …` queries stay code (JavaScript: no script engine for notes; docs/SYNTAX.md §10 "Dataview
  queries"); a Dataview `CALENDAR` was tested with the mouse and the offscreen self-test, not yet
  with a finger on a real phone.
* **Flashcards: what is still missing** — cards of the Spaced Repetition plugin and of Logseq, FSRS-6,
  the state next to each card, decks, sessions and statistics are there (docs/SYNTAX.md §15,
  docs/UI.md "Flashcards"); missing: FSRS parameters fitted to one's own reviews (an optimizer; the
  review log names no card yet), the plugin's remaining settings (the tags to ignore, "cloze patterns" of one's own, cards shown
  as inputs, a bold or curly cloze drawn as one in the editor), the recall fork's FSRS state
  kept outside the notes (`tracked_files.json`), SiYuan's cards (their state is no Markdown), picture
  occlusion, and a session checked by hand on a real phone and tablet. The
  plugin's whole-note reviews (`#review`, `sr-due:` …) are there (docs/SYNTAX.md §15 "Notes to
  review"); missing from them: the plugin's link weighting of a new note's ease, its tags and
  folders to ignore for notes, "open a random note" and "open the next note after a review", and the
  notes' review checked on a real phone.

## Tasks and calendar

* **CalDAV calendars: what is still missing** — ICS subscriptions and CalDAV accounts (events shown,
  created and edited in the calendar, their VALARMs ringing, tasks synced both ways with task lines:
  docs/UI.md "Calendars", docs/SYNTAX.md §5 "Tasks synced with CalDAV") are tested against an
  in-memory server and a real Radicale (`scripts/caldav-radicale-test.sh`). Missing: checks against
  real Nextcloud, Fastmail, iCloud and Baïkal (Digest), with Thunderbird and DAVx⁵ / Tasks.org on the
  other side and on a real phone (Android's background check, which also arms the events' alarms);
  invitations beyond answering them for the whole event (one occurrence's answer, inviting people
  from Brulion, servers without RFC 6638 scheduling — the reply is then never sent — and an
  account whose principal names no e-mail address, as Radicale's) and free/busy; Google's own CalDAV (it needs an OAuth sign-in — its secret iCal
  address works as a subscription).
* **Reminder chips in Markdown source mode and table cells** — in an edited block a ⏰ is a chip
  with its words, its menu and the caret over it as one unit (docs/UI.md); the source mode's
  TextEdit and a table cell's TextField still show the token's raw text (`⏰ 2026-11-20 18:00`).
  Source mode would need the highlighter's chips with a file position → block and range mapping for
  the dialog and the menu; a cell, a TextEdit with the highlighter in place of its TextField. Also
  untried on a real phone: the long press on an edited chip with Android's and GrapheneOS's own
  text selection handles.
* **A binding loop in the calendar's day panel** — with the side panel about 300 px wide (a
  1000 px window) a day whose entries include a closed task logs "Binding loop detected for
  property width" at PlanningItemRow's text column (`qml/planning/PlanningItemRow.qml`): the chip
  shows only on rows wider than 300 px, so a row at that width likely flips between two layouts as
  the panel's ColumnLayout settles. Seen by the agenda's self-test (`tst_views_qml_atrament_agenda`
  runs at 1200 px for now).

## Capture

* **Web Clipper in the stores** — the extension (clipper/, docs/CLIPPER.md) is loaded by hand today
  (a release attaches its Chromium ZIP and its unsigned Firefox XPI): publishing it in the Chrome
  Web Store, Edge Add-ons and on addons.mozilla.org (Firefox needs it signed; a store's Chromium id
  joins the host manifest's allowed origins), a test of the extension in Firefox (Playwright cannot
  load it there; web-ext lint only) and checks on real systems (Brave's system folder, macOS,
  Windows' registry, Flatpak browsers).
* **Audio, video and voice notes: what is still missing** — players in notes, the player view, voice
  notes and their transcripts on the device are done (docs/UI.md "Audio and video", "Voice notes").
  Missing: checks on a real phone of Qt's Android backend (playback, a video's picture and poster,
  the system's MediaRecorder writing a voice note, the microphone's permission) and of the
  AppImage's and Windows' packages with Qt Multimedia (built by CI only); players inside the
  browser version (Qt Multimedia's WebAssembly backend over the notebook's files — today a new tab)
  and recording there; text while recording on Android, whose system recorder (Qt's Android
  backend) passes no samples through the app — it needs the FFmpeg backend's buffer input or a
  recorder of its own over QAudioSource (docs/UI.md "Voice notes" → Text while recording); ggml's barrier between two
  steps of a computation still spins — Whisper now computes with fewer threads on a busy processor
  (docs/PERFORMANCE.md "Speech recognition on a busy processor"), a barrier that sleeps would need a
  change to ggml itself.
* **Photo and scan from the camera** — `/Photo` on the phone, straightening and cropping a
  document, scanning to PDF.
* **On-device automation: checks on real devices** — brulion:// links, Tasker's intents, the Quick
  Settings tiles and the launcher's shortcuts are there (docs/UI.md "Automation", docs/ANDROID.md
  "Automation"; the emulator: `scripts/android-emulator-test.sh --automation`); to be checked by
  hand: Tasker, Automate and MacroDroid on a real phone (the result broadcast in Tasker's "Intent
  Received", the provisional reminder ringing), the tiles and shortcuts in a real launcher, a
  brulion:// link from Firefox and Chrome on Linux (the desktop entry's handler) and on Windows (the
  installer's URL protocol), `registerProtocolHandler` in Chrome, Edge and Firefox.
* **New import sources: checks with real accounts and data** — OneNote, Todoist and Airtable are
  tested against fake servers and Apple Notes against a database built after Obsidian Importer's
  schema (docs/IMPORT.md); to check with real ones: OneNote's throttling of a large notebook and the
  HTML of real pages (ink, math, positioned outlines), Todoist's completed tasks (the Sync API leaves
  them out: the archive endpoint could add them), Airtable's tokens and attachment links, and
  NoteStore.sqlite of recent macOS versions (other columns, newer attachment types).

## Search

* **Searching attachments — check on devices and further ideas.** The text of PDFs, documents
  (DOCX, PPTX, ODT, EPUB) and pictures in the whole-notebook search, OCR with Tesseract and the
  jump to the hit on the page (docs/UI.md "Searching attachments") are checked automatically: the
  core's and the indexer's tests, Tesseract in the Nix dev shell and `nix flake check`, the shell
  self-test. What remains:
  * by hand: Windows with UB Mannheim's Tesseract installer (its MinGW DLL loaded into the MSVC
    program), the AppImage on Ubuntu / Fedora with the system's `tesseract-ocr` packages, PDFs'
    text on a real Android 15+ phone, a semester of real lecture scans (hundreds of MB) on a slower
    laptop — time, memory, the cache's size;
  * OCR on the phone (decided against for now: +25 MB per ABI, docs/ANDROID.md) — reconsider with an
    on-demand download of the engine and models;
  * the text of other attachments: an ODS's sheets and an ODP's slides as pages of their own (read
    as one text in parts today), documents' headers and comments; a document's text shown in its read-only view instead of
    its bytes (a hit now opens the file with "Open in another app"); DOC, PPT and RTF; the pictures
    inside documents (OCR);
  * the recognized text written into a scanned PDF as its text layer ("Make searchable…"),
    handwriting in drawings;
  * by hand: DOCX, PPTX, ODT and EPUB files made by real programs (Word, LibreOffice, Calibre, Google
    Docs exports) on a phone and the AppImage — the extractor is tested on files built from the
    formats' XML, not on files of every producer.

## Privacy and security

* **End-to-end encryption of sync: what is left** (docs/SYNC-ENCRYPTION.md; done: the design, the
  core, the setting with the password and the recovery key in the sync settings, unlocking on
  another device, a device without the key locked, a plain setting that never syncs an encrypted
  folder, changing the password, the note left in the old plain folder, turning it off, the
  guide) — a check on
  real devices: a phone opening a notebook a laptop encrypted, the key derivation's time there
  (PBKDF2, 600 000 rounds).
* **App lock and encrypted notes — check on real devices.** Checked automatically: the crypto
  against NIST/RFC vectors, Meld Encrypt's formats against fixtures made with the plugin's code
  (and Brulion's output decrypted by it), no plaintext on disk, the lock's state machine, the
  shell's self-test, the emulator (`scripts/android-emulator-test.sh --lock`: the lock screen,
  FLAG_SECURE, the widgets' snapshot, the phone's PIN through BiometricPrompt). By hand remain: a
  fingerprint and face unlock on a real phone (the emulator types the screen lock's PIN only), the
  recent apps' preview and the widgets' placeholder in a real launcher, the sensitive copy in
  Gboard's clipboard, an encrypted page edited on two devices over Syncthing and Nextcloud, and the
  same notes opened in Obsidian with Meld Encrypt itself.
* **Backups: what is left** — an Android folder chosen through a cloud document (a backup is
  written file by file into its folder, which a cloud provider's document tree does not take: it
  needs a real phone to try), and a check on real devices of "Restore from a file…": an Android
  document that is no file on the phone's storage is copied into the app's cache first, the
  browser's pick is written into its work folder — both are only exercised on the desktop (the
  browser's pick through `prepareRestoreFromBytes`, the document's copy as far as a `content://`
  URI that cannot be read).
* **Sync and import in bounded memory: what is left** — the memory of a large sync or import on a
  real phone is not measured yet, and the copy a picked folder's write falls back to where its
  document provider cannot rename (`TreeFolder.finishWrite`) has not met such a provider (every
  provider of the emulator renames) (docs/PERFORMANCE.md "Sync and import in bounded memory").

## Sharing and collaboration

* **Publishing: what remains** — publishing (docs/UI.md "Publishing", core `publish`,
  `brulion-cli publish`) is tested against in-memory servers. What remains: checking by hand against
  github.com (Pages turned on, a custom domain, the secondary rate limit of a first publish with
  hundreds of pictures; a password-protected site there), a real Nextcloud (its password policy,
  older servers' OCS answers) and WebDAV servers (Apache, nginx; a protected site's lock pages and
  sealed files served from them), on the desktop, Android and in the browser (CORS); protected
  pages in Safari and on phones' browsers (only Chromium and Firefox are tested).
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
* **Stores and repositories** — Flathub, winget, AUR and Homebrew: the manifests are ready in
  `packaging/` (`scripts/store-manifests.py` per release, packaging/STORES.md); left is the owner's
  part — submitting them (Flathub once the KDE runtime 6.12 is stable there, and the app id's
  question with Flathub; a winget PR; the AUR account; an own Homebrew tap), and trying them on
  real Windows, Arch and Homebrew. Google Play: the app for it is
  ready in the repository (docs/GOOGLE_PLAY.md); left is the owner's part in Play Console — the
  developer account, the closed test (12 testers, 14 days), the declarations (the foreground
  service's video), the first upload — and registering the GitHub/F-Droid app for Android's
  developer verification.
* **More interface languages** — translations beyond Polish and English, and with them natural
  language dates, the calculator and the introduction (Obsidian: many languages, also at the
  start).

## Windows

* **Signed files** — the installer and programs signed with a certificate, without the SmartScreen
  warning; then remove the "(test version)" label (docs/WINDOWS.md).
* **Feedback on the desktop version** — collect it from the tester (who will look at it later).
* **A mass move on the server missed by a sync** — on the Windows runners the sync right after
  1 500 pages were moved into one folder on the server now and then changes nothing at all (not
  even a download), in `tst_sync_folders` and `tst_sync_folders_streamed`
  (`manyPagesMovedAtOnce`, which syncs once more there and skips on a second miss, printing what
  the run saw); once a page renamed into a folder in the vault was refused there too (asked again
  now, and a lasting refusal skips with the vault's reason). Never seen on Linux or Android. Find
  out why on a Windows machine.

## Performance

Details in docs/PERFORMANCE.md, "Next steps":
* measuring the bottom navigation's views built in the background after the notebook opens
  (docs/PERFORMANCE.md "Built ahead"; the desktop's stalls while they are built in "Measured on
  the desktop", 2026-11-28): start-up time, frame times while they are built and memory on a phone
  and a Raspberry Pi;
* faster merging of ready notes into the index on the UI thread, less view refreshing during
  loading (in the browser the first load without a ready index takes 11–16 % longer than before
  wave 5) and measuring indexing on a Raspberry Pi and a phone (L1b);
* measuring the refresh of lists after saving (tasks, calendar, reminders, queries) on a Raspberry
  Pi (before the change 0.95 s, target < 0.2 s) and lowering its baseline row in
  tests/bench/baselines/pi.csv (L3b);
* measuring the faster undo on a Raspberry Pi (before the change 214 ms, target < 50 ms) and
  recording its baseline row in tests/bench/baselines/pi.csv — until then the performance check
  on the Pi skips it (L5b);
* measuring the sync that reads no unchanged note on a Raspberry Pi (before the change 1.35 s for
  5000 files) and lowering the `syncNoChanges` and `blockDocOps … keystroke` baseline rows in
  tests/bench/baselines (laptop.csv, pi.csv) in a quiet window (L4, L12 done on 2026-11-15);
* measuring typing in source mode (Ctrl+E) on a long page in the app on a phone and a Raspberry
  Pi (`tests/bench/app` scenario `source-typing`; docs/PERFORMANCE.md "Source mode: one edit, not
  the page": on the laptop a keystroke's handling went from 6 to 1.6 ms at 500 KB, Enter from 20
  to 1.8 ms);
* precompiled headers in the Windows, Android and browser builds (cmake/BrulionPch.cmake: on for
  Clang on Linux only, "Build times" in docs/PERFORMANCE.md): build and measure them in CI, with
  the jobs' ccache (MSVC's `/Yc` / `/Yu` from ccache 4.10 on);
* measuring the QML code compiled for size (`-Oz`) on a real phone against a build with
  `-DBRULION_QML_CODE_FOR_SIZE=OFF`: journal scrolling, opening a long page, the views — on the
  desktop no slowdown was measurable (L20b);
* **Pictures and memory: what is left** (docs/PERFORMANCE.md "Pictures and memory") — measuring a
  page of photos on a real phone (PSS, and whether one screen of reach around the view is more
  than a phone needs); the copy of every shown picture Qt Quick keeps in main memory beside its
  texture (half of what a shown picture takes where textures are main memory too: phones,
  integrated graphics); canvas cards and extension pages hold their pictures as before (they are
  small or few); the journal's kept days hold theirs for 30 s after the view is left; `NearView`
  looks at every picture frame of a window once per frame (one `mapRectToScene` each: 6 % of the
  GUI thread's instructions while a page of forty photos scrolls) — for pages of hundreds of
  pictures, look per scroller and only when it moved;
* **Memory: what is left** (docs/PERFORMANCE.md "Memory: only what is in use") — a real phone:
  PSS in use and in the background, the kept views and the away rules there, and whether a hidden
  window's graphics can be released on Android (`MemoryPolicy.setReleasesGraphics`, Linux only so
  far); letting go of the program's own pages on Android (`platform::releaseProgramPages()`; the
  system does it for cached apps by itself on recent versions) and on Windows (its working set
  can be emptied the same way); the functions' start-up order for Android and for the AppImage
  (`cmake/startup-order.txt` is made on Linux and read by lld only — names with the standard
  library's types in them differ under the NDK's, and GNU ld takes another kind of file); the
  order file is written by hand (`scripts/startup-order.py` before a release) — CI could say when
  it is stale; a view that was let go of comes back at its top, not where it was scrolled; Qt
  loads every image plugin for a PNG (53 libraries with KDE's formats installed); poppler and
  what it brings are linked, not loaded with the first PDF; the Polish translation is unpacked
  onto the heap (1.3 MB); Qt Wayland keeps a picture of the whole window for its own title bar
  where the compositor draws none (GNOME: 4 MB at 1280 × 800, 33 MB on a 4K screen); fewer
  allocator arenas on the Raspberry Pi (the desktop, 2026-11-28: −10 to −21 MB, +5.6 % CPU while
  indexing, no later `ready`; docs/PERFORMANCE.md "Measured on the desktop") — then
  `mallopt(M_ARENA_MAX, 2)` on Linux, or not;
* **File dialogs — check by hand**: Windows (Open/Save in import, export, "Image…" and the event
  log), AppImage in a real GNOME and KDE session (the portal dialog; without a desktop session
  checked only with xdg-desktop-portal-gtk under Xvfb), Android with Google Drive (import and
  "Save…"), the browser on a phone (several files at once);
* **Graph — check by hand and measure on a phone** (layout on a worker thread and drawing through
  the graphics card done, L16b): on a real phone with a large notebook panning, zooming and
  dragging pages while the layout runs; Windows with a graphics card (Direct3D 11) and the browser
  on a phone; measuring on a Raspberry Pi (software rendering) and in the Android emulator.
