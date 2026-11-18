# Brulion performance

As of 2026-10-18: the current code (branch `main` with the October 2026 optimisation package)
compared with release **v0.1.1** (`f61a2a4`) and with **d1141c0** (the last commit before the
optimisations). All three versions were measured under the same conditions and with the same
benchmarks; how to run them is in the contributors' part below. Updated on
2026-10-29 with wave 5: index memory, indexing on several cores and a ready index saved on the
device (compared with `82d0357`), and on 2026-10-30 with faster undo (L5, compared with `b3d9a3d`),
smaller packages (L9, compared with `ef90077`), faster refreshing of lists after a save (L3,
compared with `ef90077`) and a smooth graph (L16, compared with `64d0380` on a GitHub Actions server).

## Summary for users

### What got better

* **Typing in a big notebook, after the save** (2026-11-18). When you pause typing, Brulion saves
  the page; in a notebook of 10,000 notes the program then also recomputed lists you were not
  looking at — All pages sorted all 10,000 pages again, the folder tree in the sidebar was built
  anew, the references of the page you were typing in were looked for again in every note. Since
  the Journal, Pages, Search, Tasks and Calendar are kept ready in the background (2026-11-10),
  that happened after every pause. Now a list that is not on screen only notes that it is out of
  date and catches up when you open it, the folder tree changes only when files come or go, and a
  list leaves out the changes that cannot concern it; the save itself reads and checks the file
  once instead of four times, and the editor draws again only what read the changed note.
  The writing itself — checking the file, keeping the previous version, writing it safely to
  disk, reading the page again for the index — now happens on another core, as in the Zed
  editor. Measured on a computer with all those views ready: after a pause in typing the window
  no longer stops at all on an ordinary page (it used to stop for about 0.3 s), and on pages of
  5000 blocks for 0.02–0.03 s instead of 0.36–0.37 s; a letter typed into such a page shows in
  15 instead of 25 ms (the slowest twentieth).

* **Saving no longer stops the window** (2026-11-18). When you pause typing, the page used to be
  saved on the interface thread: the file read and compared, the old version kept in the history,
  the new one written and synced to the disk, the note read and indexed again — 15 ms on a small
  page on a computer (nearly all of it waiting for the disk, much more on a phone's flash), 55 ms
  on a page of 5000 blocks. Now all of that happens on another core while you keep typing; the
  interface only hands the text over (0.2–1 ms) and takes the result in (0.2 ms on a small page,
  9–11 ms at 5000 blocks). Nothing about safety changed: another program's change found at the
  save is merged as before, and renaming, deleting or closing waits for a save in progress.

* **No more hitches a minute after typing** (2026-11-18). A minute after every pause in typing
  the app used to write its ready index of the notebook again (L2), on the interface thread in
  small pieces: about 0.16 s of work at 10,000 notes on a computer, spread over a third of a
  second — on a phone 0.5–1 s of stuttering scrolling while you read. Now it is written when
  Brulion goes to the background or the notebook closes, and while you keep working at most
  every 10 minutes; the interface only takes a quick copy (about 5 ms at 10,000 notes), the
  rest happens on another core. Likewise after editing a very long note the search index used
  to read the whole note on the interface thread (about 9 ms for a note of 200 KB, 43 ms for
  1 MB, several times that on a phone); now that happens on another core too and the interface
  only takes the result (0.2–0.4 ms).
* **Typing in the Markdown source of long pages** (2026-11-16). In source mode (Ctrl+E) every
  keystroke used to hand the whole page over to be compared and saved — on a page of 500 KB
  about 18–21 ms on a Raspberry Pi 4 (a slower phone), more than half of what a frame may take,
  and again for each change of highlighting. Now only the typed text is handed over: about
  0.5–0.6 ms on that page, 0.06–0.1 ms on a page of 100 KB. In the app on a computer a letter
  typed into such a page now shows in the next frame (11.5 instead of 12.6 ms, the slowest
  twentieth 12.6 instead of 27 ms), Enter and Backspace in half the time (11–12 instead of 24–26
  ms).
* **Typing in very long pages** (2026-11-15). A keystroke used to make the page check itself
  from top to bottom — its numbering, its blank lines, its end. Now a letter typed into a block
  changes that block and nothing else: in a page of 5000 blocks the page's own work per keystroke
  is about a tenth of what it was (0.12 instead of 1.3 ms on a computer), and ticking a task,
  indenting, undo and redo in such a page cost about half.
* **A sync that has nothing to do** (2026-11-15). A notebook of 10,000 files in which nothing
  changed is checked in about 0.3 instead of 0.75 s (besides the server's answer): notes that
  have the size and time of their last sync are no longer read and compared, and the sync state
  is no longer read from disk a second time. A sync after a few changes takes about half as long
  (0.44 instead of 0.9 s).
* **A sturdier program, at no speed you would notice** (2026-11-15). Every release is now built
  with the protections Qt's own libraries are built with (a mistake in reading a damaged note
  stops the program instead of running on in a wrong state). Measured: 0–4 % more work in the
  parts that read and compute notes, about 1.7 % on average; the Android package is about 2 MB
  larger (99 instead of 97 MB), the others a few tenths of a megabyte.

* **Switching views without waiting** (2026-11-09, 2026-11-10). Journal, Pages, Search, Tasks and
  Calendar stay in memory once opened: going back to one of them (e.g. from Tasks to the Journal
  on a phone) shows it at once, where it was scrolled, instead of building it again for 0.5–1 s.
  Since 2026-11-10 they are also prepared in the background a moment after the notebook opens, so
  even the first switch to Tasks or Calendar is instant.
* **Smooth graph** (2026-10-30). The **Graph** view is laid out on a separate thread and drawn by
  the graphics card: from the very first moment you can pan it, zoom it and drag pages, and the
  interface does not wait for the layout computation. With a notebook of 10,000 notes (a graph of
  4000 pages) the graph's first frame comes after about 0.13 s instead of about 1.1 s, the longest
  stall during layout lasts about 0.2 instead of 1 s, and panning and zooming respond 2–3 times
  faster — and that on a server without a graphics card, which draws on the CPU; with a graphics
  card a frame costs a fraction of a millisecond. On a phone the graph animates at 60 frames per
  second instead of 30 (the measurement on a phone is still to come).
* **Smaller app again** (2026-11-07). The Android APK is 92.7 instead of 105.9 MB (Brulion's own
  library 45.4 instead of 58.6 MB), the browser version downloads 17.1 instead of 18.5 MB on the
  first visit (54.7 instead of 60.8 MB unpacked), the desktop program is 68 instead of 78 MB. The
  interface code generated from QML is now compiled for size; no slowdown was measurable.
* **Smaller app** (2026-10-30). The Android APK is 76.9 instead of 91.8 MiB (download size
  26.3 instead of 30.9 MB), and the browser version downloads 13.5 instead of 15.8 MB on the first
  visit — on a 20 Mbit/s connection the first frame comes about 1.1 s earlier (7.4 instead of 8.5 s).
  The packages no longer contain five unused Qt control styles and Qt Quick's fallback file
  dialog: the file dialogs are the system's. The Windows version: ZIP 42.8 instead of 49.5 MB,
  installer 29.5 instead of 32.6 MB; AppImage 53 instead of 58 MiB.
* **Refreshing after a save in large notebooks** (2026-10-30). After every save, Tasks,
  Calendar, reminders, query blocks and widgets recompute only the changed note instead of the
  whole notebook. In a notebook of 10,000 notes all this work takes about 6 ms on a computer
  instead of about 112 ms (almost 19 times less), so typing with the task list or the calendar
  open does not stall on a save. The results are exactly the same as before (tests with random
  changes check this). Queries are also twice as fast when they are computed from scratch, and
  Calendar and Agenda open a few milliseconds faster. Not measured on a phone yet (before: about
  1 s).
* **Undo and redo on long pages** (2026-10-30). Ctrl+Z and Ctrl+Shift+Z no longer reload the
  whole page, they only change the blocks the step concerns. On a page of 5000 blocks on a
  computer an undo shows after about 13 ms instead of about 33 ms — as fast as after Enter — and
  handling it takes 3–4 ms instead of about 25 ms. Collapsed blocks, zoom-in and the open editor
  stay in place after an undo, also when the undo changes their text. Typing on such a page also
  got a little faster (handling a character takes 4.1–4.2 instead of 4.7–5.0 ms).
* **Large notebooks open twice as fast and take a third less memory**
  (2026-10-29). Notes are indexed on several cores at once, and the ready index is saved on the
  device, so on the next open Brulion re-parses only the notes changed since the last time (it
  still reads the files to check that). A notebook of 10,000 notes on a computer is fully indexed
  after 0.96 s on the first open and after 0.83 s on later ones (before: about 1.8–1.9 s), and the
  index itself takes 60 % less memory (the whole app with this notebook: 340 instead of 510 MB).
  On a computer and a phone search is complete as soon as the index is ready — it is no longer
  completed in the background with a short pause of the interface; in the browser this holds on
  later visits (from the saved index), while on the first one it is completed piece by piece as
  before. In the browser the first load without a saved index takes a little longer than before
  (1000 notes: 1.3 instead of 1.1 s — the journal is loaded first and the view refreshes
  meanwhile), later ones take as long as they used to, but now with complete search.
* **Large notes no longer freeze the app.** After the start and after every autosave of a very
  large note (from about 200 KB, e.g. a long journal in one file or an imported book) Brulion
  stopped responding: for 0.9–1.6 s on a computer, for as long as 13–20 s on a slower phone.
  Now the longest pause is about 0.09 s on a computer and 0.2 s on a phone.
* **Faster start.** The window appears sooner (a notebook of 10,000 notes: on a computer
  0.45 → 0.21 s, on a phone 2.3 → 0.5 s), and so does the journal's content with a large notebook
  (computer 0.51 → 0.40 s, phone 2.4 → 1.6 s). The freeze right after loading a large notebook
  (computer 1.6 s, phone up to 19 s) is gone.
* **Typing on very long pages** (thousands of blocks): on a phone a character appears after
  about 30–40 ms instead of 70–90 ms (Enter 80 → 38 ms), on a computer handling a key costs a
  third less.
* **Views:** Tasks open twice as fast (computer 0.26 → 0.13 s, phone 1.1 →
  0.7 s), Calendar and Agenda 10–30 % faster. **The graph on a phone** is laid out in 2.6 s
  instead of 44 s (measured before the graph was laid out on a separate thread, see above).
* **Import** of long lists from HTML (e.g. an Evernote/Notion export) — 20,000 items in 0.2 s
  instead of 13 s. Block suggestions `((` are twice as fast, page suggestions `[[` about a third
  faster.
* **Android:** sync with Nextcloud over HTTPS finally works (before, every HTTPS connection
  ended with the error "TLS initialization failed"). The APK is smaller despite the bundled
  OpenSSL: 87.1 → 84.9 MB (download about 28.5 MB).
* **Browser:** a notebook finishes indexing without moving the mouse (before, it stalled until
  some event came), returning to the page is faster (the browser keeps the compiled code), the
  app's file is 0.5 MB smaller.
* **Moving and renaming** (2026-10-23): the plan of which links, tags and queries need fixing
  is computed 4–12 times faster (a folder of 500 pages in a notebook of 5000 notes: move
  0.12 → 0.03 s, rename 0.49 → 0.11 s; a page with hundreds of links to it 0.11 → 0.01 s).
  Dragging a page or folder in the tree no longer stutters over other folders: the hint
  "Updated links: …" is computed piece by piece, in the background between frames.
  Opening a page named by a popular tag is up to 2 times faster.
* **Copying blocks on a Linux computer** (2026-10-27): Ctrl+C on a whole page of 5000 blocks
  takes about 4 ms instead of about 40 ms (until the screen refreshes 13–15 instead of 46–50 ms).
  The formatted version for mail and document editors is produced only when pasting into them.
* **Scrolling long pages** (2026-11-06): a block that scrolls into view takes 20–25 % less work to
  create (its text is laid out once instead of two to four times, and the rounded shapes of tags
  and chips cost less), and the scroll bar of a page does about two thirds less work in every
  frame; making a window narrower or wider redraws a long page a quarter faster.
  On a computer scrolling was smooth before; the gain is for slower phones, where every
  block created while scrolling is a few milliseconds of a frame.
* **Pages created whole** (2026-11-07): a page of up to 1000 blocks (300 on phones and in the
  browser) creates all its blocks as soon as it opens — the ones on screen at once, the others in
  the background — so scrolling it never waits for a block to be made. It costs memory (about
  0.1–0.2 MB a block) and makes a change of the window's width slower on long pages (details
  below).

### How it is now

**On a computer** (a 2023 laptop, a notebook of 10,000 notes): the window after 0.2 s, content
after 0.4 s, the whole notebook indexed after about 1 s, on later opens after 0.8 s (meanwhile
you can already read and write). A page of 5000 blocks opens in about 0.1 s, search returns
results in a few to a dozen or so milliseconds, Tasks/Calendar/Agenda open in 0.13–0.17 s, a
graph of 4000 pages shows after about 0.13 s and can be panned at once (it is laid out in the
background). An idle Brulion uses about 0.1 % of the CPU and draws no frames.

**On a phone** (measured on a Raspberry Pi 4, which matches a slower phone, and on the Android
emulator): ordinary notebooks run smoothly — start with the sample notebook about 1.3 s, typing
on an ordinary page without delays. A large notebook (10,000 notes) shows content after about
1.6 s, but took about 15 s to index; since wave 5 it does this on several cores, and on later
opens it takes the ready index from the device (the new time on the Raspberry Pi and on a phone
is not measured yet).

**In the browser**: the first visit downloads about 13.5 MB (afterwards the page runs from the
cache and offline), returning to the page with 1000 notes draws the first frame after about
0.6 s.

**PDFs**: a PDF's thumbnail in a note is drawn once and stays in the device's cache (at most
64 MB, 32 MB on a phone), so the journal scrolls without redrawing it. The PDF viewer keeps at
most about four screens of image (phone about 41 MB, a 2560 × 1440 monitor about 59 MB), no
matter how large the PDF is and how far you zoom in; a PDF of 300 pages opens at once. The
browser version downloads about 0.14 MB more, the APK is about 0.3 MB larger.

### Known slow spots

* **Indexing large notebooks on a phone** — it was about 15 s for 10,000 notes; now the notes
  are parsed on several cores, and merging them into the index (on the interface thread, piece
  by piece) has become the longest part — the new time on a phone is not measured yet.
  The first open after a Brulion update is always "cold" (the ready index of the previous
  version is not used).
* **Memory** — a notebook of 10,000 notes takes about 340 MB on a computer (before: about
  500 MB). On phones with 3–4 GB of RAM the system may close Brulion in the background (notes are
  always saved to disk).
* **Calendar and Agenda** open after 1–1.3 s on a phone with a large notebook.
  (Undo on a very long page took about 0.2 s on a phone; since 2026-10-30 it costs as much as
  Enter — measured on a computer, not yet on a phone.)
* **The first start on a computer with a small notebook** (a fresh profile) shows content about
  0.09 s later than d1141c0 (the window, on the other hand, 0.1 s earlier); on later starts the
  content is as fast as before (details below).
* **Size** — the APK is about 93 MB (download from Google Play about 34 MB; GitHub and F-Droid
  download the APK whole), the browser version downloads about 17 MB on the first visit (L20;
  it grew from 13.5 MB when the interface was compiled to native code in 0.11.2). PDF
  pages as a drawing's background did not make the APK larger (Android renders them itself), the
  Nix package grew by about 26 MiB (poppler), the AppImage by about 2.4 MB and the Windows
  installer by about 1.8 MB (the Qt PDF library).

**Battery:** in the background on Android Brulion uses no CPU and does not wake the phone; alarms
are set only for reminders (up to 64 for the next 14 days).

---

## Performance (for contributors)

### Reference machines and method

| | machine | how |
|---|---|---|
| desktop | x86_64 laptop, Intel i5-13450HX (6 P + 4 E cores), 31 GB, NixOS | core: Release, pinned to the P-cores (`-c 0-11`), quiet-wait (`-q 3`), 5 repetitions; app: Release `-O3 -g`, Xvfb + Mesa llvmpipe (frames are rendered on the CPU — pessimistic), 5 runs (3 for first starts) |
| phone floor | Raspberry Pi 4, Cortex-A72 at 1.8 GHz, 3.7 GB | core: Release, small profile, 5 repetitions (2 rounds); app: Release, `offscreen` + Qt Quick software renderer (painting happens on the GUI thread, so stalls include it), 3 runs, 3 000-note vault (10 000 where noted) |
| Android | emulator API 36 x86_64 (4 vCPU under KVM, SwiftShader GL) on the laptop | release APKs from the launcher (`am start -W`, `dumpsys meminfo`); a debuggable measurement copy with the probe for first frame / content; optimistic — x86 vCPUs are 2–4× faster than phone cores |
| web | headless Chromium on the laptop, SwiftShader WebGL | `wasm-bench.mjs`: repeat visits with 1 000 notes in IndexedDB, 4× CPU throttling as the phone stand-in, 20 Mbit/s first visits |

Trees: **v0.1.1** = `f61a2a4`; **d1141c0** = the commit before the optimisations (its core and
app code equal `7779c04`'s; Pi and mobile binaries of `7779c04` stand for it); **current** = the
working tree of 2026-10-18 (`8a8b742` + the uncommitted N11, N13, S1 and Android fixes). All three
were built with the same toolchain (the flake lock is unchanged since v0.1.1) and measured with
the same `tests/bench` sources (same source hash). Medians; min–max or spread where it matters.

### Results: v0.1.1 → d1141c0 → current

The optimisations between d1141c0 and the current code (N-numbers below): N1 linear diacritic
folding (`text::foldKey`); N2 cheaper keystrokes on pages with tables and calculations; N3 the
vault opens after the first frame and watches folders, not files; N4 Tasks and Calendar built
once per creation; N5 time-boxed graph layout; N6 regex matching from an offset without
re-checking the whole subject; N7 faster block/page suggestions and name sorting; N8 no QML
file selector; N9 LocalStore skips unchanged files on flush; N10 long HTML lists split before
`QTextDocument`; N11 smaller APK and web build (R8, no QML debugging plugins, no developer QML);
N12 benchmark hygiene and these guards; N13 the web index timer (a repeating 0 ms `QTimer`
stalled every Qt timer in the single-threaded WebAssembly build) and the wasm code cache; S1
OpenSSL in the APK.

**Desktop app** (laptop, 10 000-note vault unless noted; `tests/bench/app`):

| scenario | v0.1.1 | d1141c0 | current | current vs d1141c0 |
|---|---:|---:|---:|---:|
| start, sample notebook (warm): first frame | 593 ms | 320 ms | 215 ms | −33 % |
| start, sample notebook (warm): content | 1.49 s | 385 ms | 392 ms | +2 % (see below) |
| start, sample notebook (cold): content | 2.03 s | 888 ms | 848 ms | −4 % |
| start, 10 000 notes (warm): first frame | 728 ms | 452 ms | 209 ms | −54 % |
| start, 10 000 notes (warm): content | 1.14 s | 508 ms | 403 ms | −21 % |
| start, 10 000 notes (first start): content | 1.29 s | 669 ms | 564 ms | −16 % |
| start, 10 000 notes (cold): content | 1.68 s | 1.03 s | 943 ms | −9 % |
| start, 10 000 notes: index ready | 5.53 s | 1.98 s | 1.93 s | −2 % |
| start, 10 000 notes: longest GUI stall after ready | 3.08 s | 1.56 s | 59 ms | −96 % |
| start, 10 000 notes: settled | 11.69 s | 5.23 s | 2.01 s | −62 % |
| heap after start, 10 000 notes | 573 MB | 498 MB | 495 MB | −1 % |
| open the 5000-block outline: frame / settled | 1.44 / 2.05 s | 117 / 283 ms | 113 / 262 ms | −4 / −7 % |
| typing, small page: key → frame | 12.7 ms | 12.7 ms | 12.7 ms | 0 |
| typing, small page: autosave, GUI blocked | 67 ms | 75 ms | 71 ms | −5 % |
| typing, 5000-block outline: key handling | 16.1 ms | 15.4 ms | 9.8 ms | −36 % |
| typing, 5000-block outline: key → frame p95 | 31.1 ms | 26.3 ms | 23.3 ms | −11 % |
| typing, 5000-block outline: Enter / undo → frame | 73 / 98 ms | 24 / 46 ms | 25 / 46 ms | 0 |
| typing, 500 KB page: autosave, longest stall (outline / document) | 0.90 / 1.56 s | 0.91 / 1.56 s | 88 / 88 ms | −90 / −94 % |
| search: key → results (incl. the 180 ms debounce) | 438 ms | 211 ms | 209 ms | −1 % |
| quick switcher: key → frame | 34.6 ms | 33.6 ms | 31.7 ms | −6 % |
| Tasks / Calendar / Agenda, first open: frame | 1.49 s / 805 / 1037 ms | 260 / 201 / 216 ms | 133 / 166 / 158 ms | −49 / −17 / −27 % |
| Graph: settled | 0.94 s | 0.79 s | 0.77 s | −2 % |
| idle: CPU / wakeups per s | 0.2 % / 10.0 | 0.1 % / 9.9 | 0.1 % / 9.9 | 0 |

One regression showed up and is fixed: after N3 the first block of a small vault appeared
~120 ms later (bisection 8f58f94 → 8a8b742, demo warm start: content 355 → 511 ms, first frame
290 → 210 ms; the GUI thread idle for ~116 ms between "index ready" and the journal's first
block). Cause: the vault now opens after the window's first frame, so the shell and its lists
exist before the index is complete; the journal's first refresh saw no notes, and the refresh
after the last index chunk waited for the lists' 120 ms debounce (`WorkspaceListModel`).
The lists now refresh on the next event-loop turn when the index becomes complete
(`tst_content_models` `journalsShowTheCompleteIndexAtOnce`). Re-measured in one session (laptop,
Xvfb, sample notebook, 5 runs; d1141c0 / before the fix / now; content = the frame showing the
first block): warm start content 384 / 506 / 392 ms with the first frame at 322 / 210 / 203 ms;
first start content 540 / 661 / 632 ms (first frame 478 / 369 / 368 ms); cold start content
898 / 825 / 829 ms. 10 000 notes: unchanged (content 396 / 400 ms, index ready 1.93 / 1.92 s).
Left: a first start (fresh home) shows the content ~90 ms later than d1141c0 (not
investigated). Under Xvfb the splash spinner animates unthrottled (no vsync) once the first
frame is up; a test build with the spinner stopped showed the warm-start content ~40 ms
sooner, so part of what is left is this measuring setup. The Pi showed no regression (content
1263 ms before N3, 1255 ms after).

**Core** (laptop, `scripts/bench.sh`, 9 suites): 365 rows compared. d1141c0 vs v0.1.1: no row
changed by more than max(10 %, noise) (geometric mean 1.004). Current vs d1141c0: **53 rows
faster, 0 slower**, geometric mean 0.766. Selected rows:

| operation | v0.1.1 | d1141c0 | current | change |
|---|---:|---:|---:|---:|
| open vault 10 000 notes (scan + read + index) | 1.26 s | 1.26 s | 1.26 s | 0 |
| first search 10 000 notes (full-text index build) | 1.07 s | 1.07 s | 557 ms | −48 % |
| search 10 000 notes, five words | 7.5 ms | 7.5 ms | 7.5 ms | 0 |
| save of a 1 MB note: search re-index | 2.54 s | 2.54 s | 45 ms | 57× |
| `foldKey` of 1 M Polish characters | 3.10 s | 3.10 s | 1.36 ms | 2283× |
| query `tag:praca` / all tasks sorted (10 000 notes) | 95 / 119 ms | 95 / 119 ms | 97 / 71 ms | +2 / −40 % |
| `((` block suggestions / `[[` page suggestions (10 000 notes) | 40 / 4.2 ms | 40 / 4.2 ms | 20.5 / 2.8 ms | −49 / −32 % |
| unlinked mentions on one 20 000-mention line | 732 ms | 732 ms | 4.3 ms | 172× |
| after one save: every consumer of `VaultIndex::changed` (10 000 notes) | 129 ms | 130 ms | 131 ms | 0 |
| keystroke in a 5000-block page (`BlockDocument`) | 1.31 ms | 1.33 ms | 1.33 ms | 0 |
| parse 20 000 × raw inline HTML in one paragraph | 2.04 s | 2.04 s | 3.0 ms | 681× |
| HTML import: a list of 20 000 items | 12.73 s | 12.75 s | 202 ms | 63× |
| Evernote note with 1000 code blocks | 75 ms | 76 ms | 34 ms | −55 % |
| LocalStore flush with 10 000 synced files | 311 ms | 313 ms | 99 ms | −68 % |
| watcher: 1000 files modified behind the app | 148 ms | 148 ms | 72 ms | −51 % |
| sync without changes, 10 000 files | 420 ms | 420 ms | 419 ms | 0 |
| index heap, 10 000 notes (22.9 MB of Markdown) | 545 MiB | 545 MiB | 545 MiB | 0 |

Scaling (`bench.sh scaling`): d1141c0 has 13 steps with k ≥ 1.6 (quadratic: `foldKey`, big-note
re-index, raw inline HTML, `<` without an autolink, unlinked mentions on a long line, HTML lists);
current has one, `calc` `evaluateLongBlock` (k 1.68, a `sum` per line bounded by the work budget,
known and accepted). The 10 000 → 50 000-note steps of search and suggestions are k 1.3–1.6
(cache effects of a 5× bigger working set, unchanged).

**Raspberry Pi 4** (app: 3 000-note vault unless noted; core: `perf-check.sh` rows, small
profile, 2 rounds × 5 repetitions; v0.1.1's core equals d1141c0's except the importers):

| scenario | v0.1.1 | d1141c0 | current | change |
|---|---:|---:|---:|---:|
| start, sample notebook (warm): first frame / content | 1.28 / 2.15 s | 1.21 / 1.46 s | 0.49 / 1.25 s | −60 / −14 % |
| start, sample notebook (cold): content | 2.75 s | 1.98 s | 1.75 s | −12 % |
| start, 10 000 notes (warm): first frame / content | 2.35 / 3.08 s | 2.30 / 2.42 s | 0.48 / 1.57 s | −79 / −35 % |
| start, 10 000 notes: index ready | 53.9 s | 15.4 s | 15.0 s | −2 % |
| start, 10 000 notes: longest stall while indexing / after ready | 4.15 / 11.4 s | 0.59 / 19.2 s | 0.54 / 0.45 s | −9 / −98 % |
| start, 10 000 notes: settled | 74.2 s | 48.2 s | 16.2 s | −66 % |
| open the 5000-block outline: frame / settled | 2.08 / 3.02 s | 712 ms / 1.34 s | 653 ms / 1.22 s | −8 / −9 % |
| typing, small page: key → frame / autosave blocked | 9.7 / 88 ms | 9.1 / 90 ms | 8.8 / 76 ms | −3 / −15 % |
| typing, 5000-block outline: key handling / key → frame p95 | 75 / 91 ms | 70 / 93 ms | 30 / 42 ms | −57 / −55 % |
| typing, 5000-block outline: Enter / undo → frame | 118 / 296 ms | 80 / 242 ms | 38 / 214 ms | −53 / −11 % |
| typing, 500 KB page: autosave, longest stall (outline / document) | 13.5 / 17.9 s | 13.2 / 19.8 s | 0.20 / 0.20 s | −98 / −99 % |
| Tasks / Calendar / Agenda, first open: frame | 1.69 / 1.92 / 1.79 s | 1.11 / 1.42 / 1.26 s | 0.73 / 1.28 / 1.04 s | −34 / −10 / −17 % |
| Graph: settled | 45.1 s | 44.0 s | 2.6 s | −94 % |
| heap after start, 10 000 notes | 475 MB | 416 MB | 434 MB | +4 % (when the search index finishes; at "ready" 373 → 375 MB) |
| core: first search, 10 000 notes | – | 5.39 / 5.43 s | 2.55 / 2.49 s | −53 % |
| core: `((` suggestions / `[[` suggestions, 10 000 notes | – | 161 / 34 ms | 80 / 28 ms | −50 / −18 % |
| core: save of a 200 KB note, search re-index | – | 1.61 s | 53 ms | 30× |
| core: `foldKey` of 300 K Polish characters | – | 8.37–8.74 s | 1.69 ms | ~5000× |
| core: raw inline HTML × 10 000 / HTML list of 5000 items | – | 2.39 s / 1.93 s | 8 / 279 ms | 300× / 7× |
| core: LocalStore flush, 10 000 synced files | – | 2.09 s | 0.95 s | −54 % |
| core: open vault 10 000 notes / after one save, all consumers | – | 8.3 s / 0.94 s | 8.4 s / 0.95 s | 0 |
| core: query `tag:praca` / all tasks sorted, 10 000 notes (3 rounds × 5) | – | 0.84–1.24 s / 817 ms | 0.97–1.27 s / 564 ms | noise (±25 %) / −31 % |

**Android** (sizes: arm64 release APKs; timings: the emulator above, x86_64 APKs, 5 launcher starts
per kind after a discarded one, PSS 10 s / 25 s after the start; probe = the debuggable
measurement copy with the benchmark probe; 5 000-note vault; d1141c0 = the `7779c04` APKs):

| | v0.1.1 | d1141c0 | current | change |
|---|---:|---:|---:|---:|
| APK arm64, `scripts/android-build.sh` (signed) | 86.40 MB | 87.05 MB | 84.94 MB (with OpenSSL, 4.67 MB) | −2.4 % |
| APK arm64 download (every entry deflated) | 28.49 MB | 28.74 MB | 28.49 MB | −0.9 % |
| APK arm64, `nix build .#brulion-android` (unsigned) | – | 87.03 MB | 84.92 MB | −2.4 % |
| APK x86_64, script / flake | 88.67 MB / – | 89.38 / 89.35 MB | 88.38 / 88.36 MB | −1.1 % |
| entries in the APK | 171 | 171 | 161 | |
| launcher start (`am start -W` TotalTime), sample notebook, warm / cold / hot | 315 / 453 / 29 ms | 313 / 464 / 48 ms | 313 / 465 / 31 ms | 0 (stops at Android's first frame) |
| launcher start, 5 000 notes, warm / cold | 277 / 454 ms | 281 / 460 ms | 313 / 458 ms | ranges overlap (266–315 vs 271–358 ms) |
| PSS after start, sample notebook / 5 000 notes | 234 / 517 MB | 161 / 386 MB | 156 / 359 MB | −3 / −7 % |
| PSS with the 5000-block outline open (5 000 notes) | 492 MB | 413 MB | 409 MB | −1 % |
| CPU time of the first 25 s, 5 000 notes | 6.44 s | 5.48 s | 3.12 s | −43 % |
| probe: first frame / content, sample notebook | 949 / 1600 ms | 910 / 1279 ms | 886 / 1298 ms | −3 / +2 % |
| probe: 5 000 notes, first frame / content / index ready | 974 / 3376 / 3242 ms | 963 / 2911 / 2823 ms | 868 / 2862 / 2824 ms | −10 / −2 / 0 % |
| probe: 5 000 notes, longest stall after ready / settled | 1.46 / 6.45 s | 1.43 / 5.71 s | 0.05 / 2.88 s | −97 / −49 % |
| probe: open the 5000-block outline, frame / settled | 387 / 824 ms | 190 / 483 ms | 176 / 411 ms | −7 / −15 % |
| probe: Tasks / Calendar / Agenda, first open | 344 / 377 / 397 ms | 230 / 268 / 261 ms | 152 / 198 / 204 ms | −34 / −26 / −22 % |
| probe: Graph settled | 46.8 s | 47.0 s | 1.54 s | −97 % |
| idle in the foreground / background | 0.03 % CPU, 0.37 wakeups/s / 0 | 0 %, 0.33 / 0 | 0.03 %, 0.37 / 0 | 0 |

After these measurements the R8 rules also keep the few androidx members that Qt's C++ calls by
name (+19 KB of `classes.dex`): the final arm64 APK is 84.96 MB, 28.50 MB to download.

The flake APK of the current code, signed with the same throwaway key, behaves like the script
build (sample notebook warm start 287 ms, PSS 157 MB; 5 000 notes PSS 359 MB). HTTPS works only
in the current APKs (OpenSSL 3 bundled; before, every HTTPS request failed with "TLS
initialization failed").

**Browser** (headless Chromium on the laptop, `wasm-bench.mjs` / `wasm-batch.sh`; 5 loads, 3 with
throttling; MinSizeRel builds, Qt 6.11.2 + Emscripten 4.0.7):

| | v0.1.1 | d1141c0 | current | change |
|---|---:|---:|---:|---:|
| `brulion.wasm` raw / gzip -9 / brotli -q 11 | 40.42 / 13.69 / 9.42 MB | 40.82 / 13.81 / 9.50 MB | 40.28 / 13.68 / 9.41 MB | −1.3 / −1.0 / −1.0 % |
| first visit (empty storage, localhost): first draw | 1106 ms | 1059 ms | 883 ms | −17 % |
| repeat visit, empty storage: first draw | 959 ms | 946 ms | 586 ms | −38 % |
| repeat visit, 1 000 notes: first draw | 935 ms | 950 ms | 606 ms | −36 % |
| repeat visit, 1 000 notes: index ready without input | never (stalls until input) | never | 1.21 s | fixed (N13) |
| repeat visit, 1 000 notes: main thread busy / longest task | 1776 / 516 ms | 1564 / 451 ms | 1255 / 174 ms | −20 / −61 % |
| ×4 CPU, 1 000 notes: first draw / index ready / settled | 3179 ms / never / 6.94 s | 3200 ms / never / 6.20 s | 1635 ms / 4.27 s / 4.91 s | −49 % / fixed / −21 % |
| ×4 CPU: longest task | 1904 ms | 1845 ms | 603 ms | −67 % |
| first visit at 20 Mbit/s, gzip / brotli: first draw | 8.31 / 6.05 s | 8.23 / 7.42 s | 7.69 / 6.93 s | −7 / −7 % (brotli runs noisy: 5.8–7.4 s) |

"Settled" of the older builds stops early because their index stalls without input (N13), so
it understates their real time to a usable search.

### Wave 4: folders, tags as pages, selection (2026-10-23)

The integration of the six features (folders, tags as page links, block links, block selection,
keyboard toolbar, agent CLI) brought new hot paths; their benchmarks are in `tests/bench`
(`bench_index` `planRelocation`, `moveImpact`, `relocationSteps`, `tagPageOpen`, `suggestTags`;
`bench_markdown` `selectAllCopy`; `bench_io` `clipboardHtml`) and the probe's `select-copy`
scenario (`tests/bench/app`). `relocationVault()` (bench_index) is `bench::makeVault()` with a
tenth of the notes in a folder `Projekty` (500 of 5000), path-qualified links, bracketed tags and
query blocks naming it, and the pages tags name.

**Core** (laptop, Release, P-cores, 5 repetitions, the machine shared with other builds: `-q 6`;
before = `c92ab1d`, the benchmarks on the code as merged, after = this branch):

| operation | before | after | change |
|---|---:|---:|---:|
| plan a move: folder of 500 notes moved into another (5000 notes) | 121 ms | 25.5 ms | −79 % |
| … the folder renamed (its path-qualified links, tags and queries rewritten) | 487 ms | 109 ms | −78 % |
| … the most linked page renamed (567 notes rewritten) | 113 ms | 9.1 ms | −92 % |
| … the same, 20 000 notes: folder moved / renamed / page renamed | 839 / 2290 / 462 ms | 133 / 460 / 39 ms | −84 / −80 / −92 % |
| `notebook::relocationImpact` (move dialog, drop), 5000 notes on disk: folder moved / renamed / page moved | 123 / 489 / 25 ms | 25.5 / 110 / 6.9 ms | −79 / −77 / −73 % |
| the dragged row's ghost, GUI thread per event-loop turn (5000 notes, folder moved) | one stall of 123 ms | 3.3 ms, then ≤ 8.5 ms × 3 turns | |
| … 20 000 notes | one stall of 839 ms | 16 ms, then ≤ 8.7 ms × 15 turns | |
| open the page `#praca` names (5000 notes): warm / right after a save | 2.7 / 6.5 ms | 1.2 / 4.8 ms | −57 / −26 % |
| … 20 000 notes | 15.5 / 29.5 ms | 8.1 / 22 ms | −48 / −25 % |
| `#` completion (`suggestTags`), 5000 notes: 'pra' / 'stro' / nothing typed | 0.59 / 1.44 / 0.014 ms | same | |
| select all + copy, 5000 blocks: top rows + Markdown / plain text / HTML flavour | – | 0.42 / 11.4 / 34.5 ms | new |

What was not linear or too slow, and what changed:
* **Move plans** looked at every note linking to a moved name as new text: `findQueryNames()` (a
  case-insensitive search through the whole note, then a parse) and `retargetTags()` (a parse) for
  each, links re-keyed and resolved twice, and every `path:` filter folded every path of the vault
  (O(query notes × notes): folder move k 1.40 between 5000 and 20 000 notes, now 1.19; rename 1.12
  → 1.04). Now the index's own link keys and resolutions are used, only notes with a query block or
  a renamed tag are parsed, `retargetLinks()` reuses the parsed links, `path:` prefixes are a range
  in a sorted list of folded paths, and the table of names after the move is an overlay on the one
  before (no copy of the whole table). `paths::normalize()` / `stripMarkdownExtension()` lost their
  split-and-join and regular expression, the name table lowers ASCII without Qt's Unicode tables.
* **The drag ghost** asked `Workspace.moveImpact()` synchronously 150 ms after each target change:
  `Workspace.requestMoveImpact()` now plans 8 ms per event-loop turn (`RelocationPlanner`), drops a
  request when the target changes and keeps impacts per target until the notebook changes.
* **Tags as pages**: the page-level tags of every tagged note were worked out again on every page
  open (and per note in every query with a tag filter); the index entry keeps them.
* The remaining super-linear rows are memory, not algorithm: `suggestTags` 5000 → 20 000 notes k
  1.36–1.53 (the `suggestPages()` scan, as `suggestPages` itself at 10 000 → 50 000) and the warm
  tag page k 1.4 (per-block reads of every tagged note); "after a save" stays k 1.10.

**App** (laptop, Xvfb + llvmpipe, Release, 10 000-note vault, 5 runs; `select-copy`: Ctrl+A twice,
Ctrl+C, Escape, three rounds per run):

| | outline 5000 blocks | document 5000 blocks |
|---|---:|---:|
| select every block (2nd Ctrl+A): key handling / key → frame | 8.1 / 18 ms (p95 44 ms) | 8.6 / 18 ms (p95 43 ms) |
| copy (Ctrl+C): key handling / key → frame | 3.8 / 15 ms (p95 4.5 / 16 ms) | 4.1 / 13 ms (p95 5.4 / 16 ms) |
| … before the lazy HTML flavour (same laptop and session) | 38 / 46 ms | 44 / 50 ms |
| copied | 478 190 characters | 597 309 characters |

A copy of a whole 5000-block page cost about 55 ms on the desktop when measured first (53 / 62 and
59 / 68 ms then), two thirds of it the HTML flavour (`io::clipboardHtml`, linear: 3.5 / 34.5 /
140 ms for 500 / 5000 / 20 000 blocks). The flavour is now rendered only when another application
asks for it (`LazyHtmlMimeData`, docs/UI.md "Selecting and copying"; a paste into Brulion, a code
editor or a terminal never renders it): Ctrl+C on the same pages takes ~4 ms of handling and
13–15 ms to the frame — 10× / 3–4× less (2026-10-27, `select-copy`, base `102920e` against this
change, two rounds of 5 runs each, the second on an idle laptop: the numbers above). The cost
moves to the first paste into a mail or office app (34.5 ms at 5000 blocks, once). Android, the
browser and Windows take the clipboard's data at once (Windows: `OleSetClipboard` enumerates the
formats, and Qt's CF_HTML converter asks for `html()` to offer its format), so they still render
it while copying.

### Wave 5: index memory (L8), parallel indexing (L1), index cache (L2) (2026-10-29)

Three steps on the index, each committed and measured on its own (laptop, Release, P-cores,
`-q 3`, 5 repetitions; base = `sweep2` at `82d0357`, the same `tests/bench` sources on both trees —
rows the base cannot build are skipped there, `__has_include` in `bench_index.cpp`).

**L8 — memory.** The heap of the index of the 10 000-note bench vault (22.9 MB of Markdown;
`bench_index:memory`, glibc malloc statistics, exact) step by step:

| step (commit) | ParsedNote only | index | + full-text index |
|---|---:|---:|---:|
| base | 352.2 MB | 447.9 MB | 571.7 MB (545.2 MiB) |
| index maps: per-key path sets as sorted lists ≤ 32 paths (`smallsets_p.h`) | 352.2 | 382.1 | 505.9 |
| `Block::task` on the heap (`Boxed<Task>`, Block 440 → 232 bytes) | 287.6 | 317.5 | 441.2 |
| no growth slack (blocks reserved exactly, lists squeezed) | 256.9 | 286.8 | 410.4 |
| `ParsedNote::blockIds` a `QMap` | 246.1 | 276.0 | 399.7 |
| `Properties` a `QMap` | 214.9 | 244.8 | 368.5 |
| `Block::raw` a slice of the note's text (`BlockSource`) | 165.7 | 195.5 | 319.3 |
| full-text documents point into the dictionary, posting lists squeezed | 165.7 | 195.6 | 232.3 |
| **after (final tree)** | **165.7 (158.0 MiB)** | **194.5 (185.5 MiB)** | **231.3 (220.6 MiB), −59.5 %** |

50 000 notes: 2681.7 → 1094.9 MiB (−59.2 %). The model's counts (the `memory` row's info line,
equal on both trees): 244 795 blocks, 44 919 tasks, 11 413 blocks with properties, 18 064 with an
id, 65 815 links (6 104 block refs), 21.5 M characters of raw and 19.2 M of block text. Where it
went (heaptrack on the base, peak of the 50 000-note rows): the `QList<Block>` storage with its
growth (Blocks of 440 bytes, an in-place `std::optional<Task>` in each), the `raw` and `text`
joins (234 and 200 MB), the full-text index's per-document term copies (183 MB of strings +
157 MB of lists), `ParsedNote::tasks` and `links` growth (65 + 54 MB), and nearly empty 48-entry
QHash/QSet spans (a QSet<QString> with one element ≈ 1.4 KB) for every link key, block id, tag and
name of the index, every `blockIds` and every block's `Properties` (`collapsed::`, a Logseq `id::`).
`QMap` was taken for `Properties` and `blockIds` instead of the planned own compact classes: the
same saving within 1 MB, no new API (iteration is by key now, deterministic — QHash's was random).
Times did not suffer: `buildInMemory` 10 000 notes 836 → 797 ms (−4.6 %), `parseTypical` −5 to
−12 %, `firstSearch` 465 → 475 ms (+2 %, within the noise).

**L1 — parse on worker threads.** `notebook::IndexLoader` reads (`Vault::readDetached`),
decodes and parses the notes (`VaultIndex::prepare`) on a `WorkerPool`, builds the full-text index
on one more thread (`SearchIndex::prepare`/`set`, adopted with per-document content keys) and merges
the notes on the owner's thread in slices (`updatePrepared`, the vault's bookkeeping by
`adoptRead`). The app's pool: clamp(cores − 1, 1, 4) low-priority workers + 1; WebAssembly keeps
the slices on the GUI thread. `openFromDisk` = scan + read + index, until every note is merged
(the loader rows: also the full-text index built and adopted):

| | 1 000 notes | 10 000 notes | 50 000 notes |
|---|---:|---:|---:|
| before: one thread (`readText` + `update`, the app's old path, no full-text index) | 91 ms | 965 ms | 5045 ms |
| loader inline (no threads: WebAssembly) | 93 ms | 979 ms | 5065 ms |
| loader, 1 worker | 79 ms | 793 ms | 4126 ms |
| loader, 4 workers (+ full-text index) | 34 ms | 352 ms | 1834 ms |
| `VaultIndex::prepare` of every note on one thread | 62 ms | 621 ms | – |

The owner's merge (resolving and relinking every note's links, the tag and block maps, the vault's
read bookkeeping) is now the larger part: 4 workers give 2.7× of the serial path, not 4×.

**L2 — the index cache.** `<device state>/index-cache.bin` (`index/indexcache.h`; in the browser a
cache mount of its own, docs/WEB.md: a quota failure there loses only the cache, and
`indexCacheBudget()` keeps it within half of the free quota): every note's parse (block texts and
raw lines stored as offsets into the note where they are one piece of it), the full-text index, a
directory checked per note by path, byte size and the content key of the text just read; identity
= format, `markdown::ParserVersion`, `SearchIndex::FormatVersion`, Qt and app versions, the
config's parse fingerprint, the vault id. 10 000 notes: 18.9 MB (0.82× the
vault), encoded in 122 ms (in 8 ms slices on the GUI thread; since 2026-11-18 on a pool thread
from a snapshot, "Index cache and search upkeep off the GUI thread" below), all notes and the full-text index
decoded in 129 ms on one thread (k 1.03 / 1.06 between 10 000 and 50 000).

| `openFromDisk` from the cache | 1 000 notes | 10 000 notes | 50 000 notes |
|---|---:|---:|---:|
| inline (WebAssembly) | 51 ms | 552–578 ms (−41 % of the cold inline load) | 3136 ms |
| 4 workers | 22 ms | 272–284 ms | 1604 ms |

Every file is still read and hashed (exact invalidation, the vault's bookkeeping unchanged), so a
warm load saves parsing and tokenising, not reading. Heap after a load from the disk with the
loader (vault, texts, index, full-text index): parsed 305.5 MiB, from the cache 307.2 MiB (+0.6 %).

Since the review, decoding also checks every line number of a record against the text's line
count (a record that passed the size and 64-bit content-key check but belongs to another text
must not point past the note's end; one branch-free pass over the text). Laptop, Release, `-c 0-11
-q 3`, `a5f6c57` against the fix, two alternating rounds of 5 runs (medians): `cacheDecode` 10 000
notes 133.5 / 135.3 → 139.4 / 133.9 ms, 50 000 notes 815 / 818 → 841 / 801 ms; `openFromDisk`
warm cache with 4 workers 292 / 288 → 290 / 270 ms, inline 567 / 531 → 573 / 538 ms — within the
noise (a first, per-character version cost +7 % in `cacheDecode`).

**App** (laptop, Xvfb + llvmpipe, Release `-O3 -g`, the 10 000-note bench vault of
`make-bench-vault.py`; `first` = a fresh profile, i.e. no index cache; `warm` = the same profile
again, i.e. the cache written by the warm-up run; 3 runs of `first`, 5 of `warm`, medians, base and head alternated, the second of two rounds on a quiet laptop):

| | before: first / warm | after: first (cold index) / warm (from the cache) |
|---|---:|---:|
| index ready (`Workspace.ready`, from the process start) | 1891 / 1723 ms | **964 / 831 ms** (−49 / −52 %) |
| longest GUI stall while indexing | 29 / 29 ms | 28 / 32 ms |
| longest GUI stall after ready (the first full-text build) | 54 / 54 ms | **0 / 0 ms** |
| frames per second while indexing | 39 / 40 | 47 / 48 |
| malloc heap when settled | 512 / 504 MB | **340 / 332 MB** (−34 %) |
| RSS when settled | 664 / 647 MB | 504 / 490 MB |
| CPU time until settled (all threads) | 4.49 / 4.35 s | 4.48 / 3.56 s |
| first frame / first content | 317 / 591 ms, 184 / 449 ms | unchanged |

The targets were 0.9 s cold and 0.6 s warm: the merge on the GUI thread (12 ms slices between
frames) is now what the index waits for — next step below (L1b). The full-text index no longer
costs the GUI anything: it is built on its own thread (or read from the cache) and adopted at once,
so the first search after "ready" finds a complete index. Not so in the browser on a first visit
(and in any inline load whose cache has no full-text section): there is no thread for it, so the
documents are still built in 8 ms slices after "ready", and a search before they are done builds
the rest at once, as before wave 5.

Not measured in this wave (no quiet window on the Raspberry Pi — another agent built there —, the
emulator needs Android builds of both trees): the Pi and the Android emulator (PSS at 5 000 notes)
numbers below in Budgets are the previous ones, except where noted.

**Browser** (measured after the review, laptop, headless Chromium 155 + SwiftShader,
`wasm-bench.mjs` with the 1 000-note `web1k` vault in IndexedDB, MinSizeRel builds of `82d0357`
and of this branch, two alternating rounds; medians of 5 repeat visits at 1×, 3 at ×4 CPU, the
two rounds side by side; "cold" = no index cache, "warm" = `--wait-cache`, all 1 000 notes from it):

| 1 000 notes | before | after, cold | after, warm |
|---|---:|---:|---:|
| index ready, repeat visit | 1116 / 1117 ms | 1280 / 1315 ms (+16 %) | 1099 / 1128 ms (±0) |
| settled (main thread quiet) | 1498 / 1513 ms | 1625 / 1645 ms (+8 %) | 1295 / 1252 ms (−15 %) |
| main thread busy | 1223 / 1096 ms | 1150 / 1172 ms | 974 / 1131 ms |
| ×4 CPU: index ready | 5292 / 5147 ms | 5786 / 5735 ms (+11 %) | 4767 / 4690 ms (−9 %) |
| ×4 CPU: settled | 5900 / 5779 ms | 6368 / 6237 ms (+8 %) | 4875 / 4776 ms (−17 %) |
| ×4 CPU: long tasks in total / longest | 3458 / 722 ms | 2343 / 927 ms | 2365 / 942 ms |
| ×4 CPU: WebGL draw calls / frames while loading | 2337 / 81 | 5765 / 105 | 3733 / 68 |
| first visit (cold): index ready, 1× / ×4 CPU | 1419–1506 / 5952–6593 ms | 1643–1657 / 7293–7437 ms | – |
| wasm linear memory | 104 MB | 86 MB (−17 %) | 86 MB |

The cold load in the browser got slower to "ready" although the main thread does about the same
work (+0–3 %): the journals are merged first, so the visible journal fills in early and the UI
draws 30 % (×4 CPU: 2.5×) more while the rest loads, and that time is taken from the slices
(first draw unchanged at ~0.6 s; when the journal's content shows was not measured). The
warm load (every later visit once the cache is written, ~5 s after "ready") is as fast as the old
cold one at 1× and faster at ×4 CPU, with the full-text index included. Cutting the redraws while
loading is part of L1b. (wasm-bench closes the page ~1.5 s after it settles, before the cache is
saved: without `--wait-cache` its repeat visits are cold.) `scripts/wasm-smoke.mjs --loads 2 --index-notes 3000` (wasm.yml) checks that
the browser's second load takes the notes from the cache; on a GitHub runner (headless Chromium,
no throttling, times from the navigation): index of 3000 notes complete at 4.99 s on the first
load (cold, in slices on the GUI thread), the cache saved at 10.1 s, and on the reload complete at
2.73 s with 3000 of 3000 notes from the cache (−45 %).

### L5: delta undo/redo (2026-10-30)

An undo step was a delta of the whole page text, and undo / redo turned the page into the other
version by reading it again: `BlockListModel::replaceContent()` parsed the page (`BlockDocument`,
every block's `parseNoteWith()`), matched the old and new blocks (LCS) and removed and inserted the
rows of every changed block. Now a step is a `BlockDocument::Delta` — the run of blocks the command
changed (from the first to the last changed block), a block in both versions with only its changed
range of text — and undo / redo apply it as a document operation (`revert` / `reapply` through
`BlockListModel::apply()`): only the changed blocks are parsed, every block keeps its uid and row
(`dataChanged` for a changed one). Block uids are unique in the process, and `replaceContent()`
gives the blocks it keeps their old uids, so whole-text edits (Markdown pasted, a template, the note
mode, source mode) are small steps too. A command takes `version()` before it and `diffFrom()` after
it instead of serialising the page and comparing two strings; typing merges `composed()` the
keystroke's delta with the step (bounded: a step whose run spans more than 256 blocks — the first
keystroke at the top of a page also renumbers a loose ordered list at its end — stays a step of its
own, or every later keystroke would compose thousands of blocks).

**Core** (laptop, Release, P-cores, `-q 3`, 5 repetitions; `bench_markdown:blockDocOps` on the
5000-block outline, from the branch's perf-check run; the rows before L5 measured alternately with
`sweep2` at `b3d9a3d` in the same session):

| operation | 5000 blocks | 500 blocks |
|---|---:|---:|
| `undo+redo`: an Enter's delta reverted and reapplied (one undo + one redo) | 1.59 ms | 0.111 ms (k 1.16) |
| `re-read`: the page read again, unchanged blocks lending their parse — the core of the old undo, which had no parse to borrow, and matched the blocks and moved the rows on top | 3.44 ms per step | 0.340 ms |
| `version+diff`: a `setBody` with the step's bookkeeping (`version()` + `diffFrom()`) | 1.11 ms (`setBody` alone 1.00 ms) | 0.093 ms |
| `keystroke`, `setBody`, `split+merge`, … against `b3d9a3d` | within noise (+1 to +9 %, no row outside max(10 %, noise)) | |

**App** (`tests/bench/app`, laptop, Release, Xvfb; the `typing` scenario on the 5000-block pages of
the bench notebook; 6 runs per build, base and branch interleaved; medians, min–max in brackets):

| `typing-outline5000` | before | after | change |
|---|---:|---:|---:|
| undo (Ctrl+Z) → frame | 32.1 ms [31.7–32.2] | 13.2 ms [12.6–13.8] | −59 % |
| … its handling | 24.7 ms | 3.6 ms | −85 % |
| redo (Ctrl+Shift+Z) → frame | 34.6 ms [34.0–35.4] | 13.9 ms [13.5–14.1] | −60 % |
| … its handling | 23.7 ms | 3.1 ms | −87 % |
| Enter → frame | 14.6 ms | 14.7 ms | 0 |
| keystroke handling, median / p95 | 4.7 / 9.4 ms | 4.1 / 8.7 ms | −13 / −7 % |
| keystroke → frame, p95 | 15.2 ms | 14.3 ms | −6 % |
| anonymous memory after the scenario | 408 MB | 402 MB | −6 MB |

`typing-document5000` (5000 paragraphs, document mode): undo → frame 33.4 → 12.7 ms (handling
26.2 → 3.4 ms), redo 37.0 → 13.2 ms (26.3 → 3.1 ms), Enter 14.3 → 13.8 ms, keystroke handling
5.0 → 4.2 ms (p95 9.3 → 8.5 ms). The autosave after a burst of typing is unchanged within its noise
(the GUI time it blocks: outline 149 → 139 ms, document 124 → 130 ms, the ranges overlapping).
Undo and redo now cost what Enter costs (the budget below); before the bound on typing merges the
first keystroke on the document page made every later one 1.1 ms slower (above).

What the step holds: `undoStoresChangesNotPages` (3000 blocks, 60 Enter + typing steps: history <
a quarter of the page) and `pasteIsASmallUndoStep` (3 pasted blocks on a 3000-block page: < 1000
characters) guard it; one character typed into a 100 000-character code block is a step of a few
characters (`deltaHoldsChangesOnly`). The Raspberry Pi was not measured (no builds on it in this
sweep; L5b in "Next steps"); its undo was 214 ms before, five times its Enter (38 ms), and the
work an undo does on the laptop fell by 85 %.

`tst_editor_controller` `largePageTouchesOneRow` guards the ratio without timing a machine: the
median of five undos (and five redos) of an Enter on a 5000-block page must stay ≤ 3 × the median
Enter + 2 ms (Debug build: Enter 2.8 ms, undo 2.0 ms, redo 2.0 ms).

### L9: native file pickers, one Qt Quick Controls style (2026-10-30)

The packages carried five Qt Quick Controls styles and QtQuick.Dialogs that the app never shows,
for two reasons found in Qt 6.11.2's sources: qmlimportscanner follows every `import … auto` of a
qmldir, optional ones included, and every deploy step (androiddeployqt, the browser build's
`qt_import_qml_plugins`, windeployqt, linuxdeploy-plugin-qt) packages what it reports. (1)
`QtQuick/Controls/qmldir` has `optional import QtQuick.Controls.{Fusion,Imagine,Material,Universal,
FluentWinUI3} auto`, so five files' `import QtQuick.Controls` (run-time style selection; the app
forces Basic anyway) deployed all five styles; (2) QtQuick.Dialogs' non-native fallback dialog
exists once per style (`quickimpl/qml/+Material/…`) and imports each style and
`Qt.labs.folderlistmodel`. Now the QML imports `QtQuick.Controls.Basic` (compile-time style
selection) and every picker is `FilePicker` (`src/app/ui/filepicker.h`, docs/UI.md "File
pickers"): the platform theme's own dialog helper — the code path QtQuick.Dialogs took on Android
(SAF intents) and Windows (`IFileDialog`) — and Qt Widgets' `QFileDialog` on desktops without one;
the browser picks through `QWasmLocalFileAccess` (`src/platform/wasm/wasmfiles.h`) and has no Qt
Widgets any more. Guards: configure stops on a bare `import QtQuick.Controls` / `QtQuick.Dialogs`
(`src/app/CMakeLists.txt`); `scripts/android-build.sh` / `nix/android.nix` fail on a style or
QtQuick.Dialogs library in the APK, `scripts/wasm-build.sh` on such a module in the app's QML plugin
list.

Measured on the laptop in one session, `sweep2` at `ef90077` (base) against the branch, same
toolchains (Qt 6.11.2, NDK r27c, emsdk 4.0.7):

| | base | L9 | change |
|---|---:|---:|---:|
| APK arm64 release (`android-apk.sh … rel`, signed) | 96 230 481 B (91.77 MiB) | 80 601 163 B (76.87 MiB) | −15 629 318 B (−16.2 %) |
| APK arm64 download (every entry deflated, `size-report.py`) | 30.86 MB | 26.26 MB | −4.60 MB (−14.9 %) |
| APK entries | 191 | 166 | −25 |
| … Qt Quick Controls styles not used (19 entries) | 11.80 MB (3.80 MB deflated) | 0 | |
| … Qt Quick Dialogs (5 entries) | 3.02 MB (0.83 MB deflated) | 0 | |
| … `libbrulion` (FilePicker, the dialog helper's glue) | 30.94 MB | 31.06 MB | +0.12 MB |
| `brulion.wasm` raw (MinSizeRel, `scripts/wasm-build.sh`) | 47 225 371 B | 41 909 609 B | −5 315 762 B (−11.3 %) |
| `brulion.wasm` gzip -9 | 15 803 378 B | 13 498 551 B | −2 304 827 B (−14.6 %) |
| `brulion.wasm` brotli -q 11 | 10 841 274 B | 9 231 654 B | −1 609 620 B (−14.8 %) |
| wasm link map (`wasm-linkmap.sh`, before wasm-opt): code / data | 36.88 / 16.51 MB | 35.25 / 12.64 MB | −1.63 / −3.87 MB |
| … styles not used / QuickDialogs2QuickImpl + Dialogs2 + Utils / Qt Widgets | 2.75 / 2.41 / 0.52 MB | 0 / 0 / 0 | |
| web first visit at 20 Mbit/s, gzip (`wasm-bench.mjs --visits first --encoding gzip --throttle 20000,40`, 2 rounds × 5): `.wasm` transferred | 15.80 MB | 13.50 MB | −2.30 MB |
| … first draw, median per round | 8.42 / 8.49 s | 7.38 / 7.32 s | −1.1 s (−13 %) |
| … main thread busy during the load | 1.64 / 1.58 s | 1.43 / 1.45 s | −0.17 s |
| Windows ZIP / installer (`windows.yml`, GitHub Actions: `sweep2` run at `ef90077` and the branch, same day) | 49.5 / 32.6 MB | 42.8 / 29.5 MB | −6.7 / −3.1 MB (−14 / −10 %) |
| AppImage x86_64 / aarch64 (`appimage.yml`, both trees the same day) | 60 492 280 / 60 529 160 B | 55 859 704 / 55 953 928 B | −4.42 / −4.36 MiB (−7.7 / −7.6 %) |

(The 20 Mbit/s runs are network-bound; another agent's build ran on the laptop during them, the
same for both builds.) The AppImage needed one more step: linuxdeploy-plugin-qt copies the folder of
every QML import whole, so `import QtQuick` brought all of `qml/QtQuick` — the styles and
QtQuick.Dialogs stayed after the QML changes alone (the AppImage was 0.1 MB larger);
`scripts/appimage-build.sh` now deploys, removes them (their QML folders and libraries) and then
packs, and its check fails when one is back. Still packaged: QtTest, QuickTest and `Qt.labs.folderlistmodel`, imported by
the developer and self-test QML under `src/app` (`qml/dev`, `qml/content/*SelfTest.qml`) — APK
0.84 MB, wasm 0.27 MB before wasm-opt. qmlimportscanner scans the target's whole source folder,
compiled or not, and its `-exclude <folder>` does not keep a folder out in 6.11.2 (it also visits
each folder's `.` entry), so they need a folder of their own outside `src/app` (L9b below).

Checks: `tests/app/filepicker` (fake backends, the widget dialog offscreen), `tst_pickedfiles`,
the import self-test's save dialog (`tst_appio_import_qml*`), `scripts/wasm-smoke.mjs
--pick-files` (CI: seven files through the browser's `<input>`, one above the limit per file
never read, the reading stopped at the pick's total, and a download), the Playwright tests' export (`showSaveFilePicker` in Chromium, a download in
Firefox) and `scripts/android-emulator-test.sh --pickers` (DocumentsUI's open, folder and save
pickers on Android 16).

### L3: incremental derived lists (2026-10-30)

Every `VaultIndex` change used to drop all derived lists, and everything listening to
`VaultIndex::changed` computed its view of the whole vault again: the reminder scheduler (up to four
passes per save on desktops), the Tasks view, the Calendar / Agenda and the side panel's month,
every ```query block of the open pages, the Android widgets. Now:

* **The index logs and patches.** Each change records the notes it touched — the note (both paths
  of a rename), notes whose links now resolve elsewhere (relink), notes whose `((id))` show one of
  its block ids — and whether names changed (a note or attachment added, removed or renamed, aliases):
  `changesSince(version)`, a log of 1024 changes / 16 384 paths (each record's paths in a compact
  set: ~60 bytes for the usual single note, not a 1.4 KB hash span), incomplete after `clear()`, a
  re-parsing `setConfig()` or an overflow; `serial()` tells indexes apart. The lists built once are
  patched with the changed note instead of dropped: sorted entries (by path and by relevance),
  `paths()`, `tasks()`, `reminders()` (the note's segment), `journals()`, `notesWithQueries()`; tags,
  missing pages, tag targets and the page-property lists are dropped only when the change touches
  what they list (its tags and their spellings, its unresolved links, names, its page properties).
  Lists nobody read for 32 changes (a bulk load) are dropped and built once at the next read.
* **Consumers follow the log** (`index/notecache.h`: `IndexFollower`, `NoteCache<Value>`): per-note
  values, computed again for the logged notes only; anything resolving names (`page:`, `ref:`) goes
  full when names change, and another key (query, day, range, language) is a full sync.
* **Three core caches** share their per-note code with the free functions, which are the caches
  without history: `QueryCache` (a query's matches in result order — position and sort keys, no
  `Task` copies; the changed notes evaluated, sorted and merged in; `remind:` filters always full),
  `CalendarCache` (each note's task and reminder items of the range), `NotificationCache` (each
  note's occurrences of a window of days; requests reaching outside it are computed directly; the
  local time zone is part of its key — the kept instants are computed again after a change, so
  reminders keep their wall-clock times).
* **The app keeps them**: `ReminderScheduler` (one `NotificationCache` of [today − 8 days,
  today + horizon + 1] for its refresh, the in-app firing checks and the missed-list pruning),
  `TaskListModel` (a `QueryCache`; the changed notes' tasks — taken in one pass over the cache's
  matches, however many notes one change names — merged into its sorted list, page titles compared
  by ranks — a merge bringing a new title rebuilds), `CalendarModel` (a `CalendarCache`;
  the overdue list from an `OverdueCache`: the old scan of open tasks due before today, per note),
  `Workspace` (the last 4 calendar ranges and 8 queries: the side panel's month, query blocks; a
  query matching more than 5000 items is not kept — its cache would hold every match, ~0.2 KB each,
  for the session),
  `WidgetPublisher` (the snapshot's caches kept across publishes).
* **Habits** (added later, `habits/habitcache.h`) follow the same log: a `NoteCache` of each note's
  habit definitions and checks (only notes with any are kept — the habits' pages and the daily
  notes with checks), the merged lists rebuilt from those values after a change (never from the
  whole vault) with a revision per habit; `HabitListModel` computes a habit's streaks and month
  grid again only when its revision moved (a tick in one daily note: that habit's row alone;
  `tst_habits` cacheManyNotes and `tst_planning_habits` incremental check it). Statistics walk at
  most the last 3660 days of a habit.
* **Full syncs stay as cheap as the old full computation.** A first view, another range or the
  date picker's marks compute everything: a `NoteCache` then fills its values from the index's own
  task and reminder lists (contiguous, kept up to date by the index) rather than visiting 10 000
  notes one by one, one-off reminders outside the range are skipped before they are copied, and a
  query run walks the sorted note records, keeps its sort keys inline, sorts through a permutation
  and visits only a note's task (or reminder) blocks. Without that the app's first open of the
  Calendar was ~50 ms slower (it asks for several ranges; each full computation looked up every
  note) — caught by the app benchmark below.

**Core** (laptop, Release, P-cores, `-q 2`, 5 repetitions; `bench_reminders:afterEdit`, a journal of
the mixed 10 000-note vault edited back and forth, the caches synced once before the loop; `sweep2`
at `ef90077` measured right before in the same session):

| after one save, mixed vault 10 000 notes | before | after (the app's path) | free function after |
|---|---:|---:|---:|
| `update` alone | 0.019 ms | 0.020 ms | – |
| `reminders()` / `tasks()` | 6.2 / 13.5 ms | 0.020 / 0.020 ms (patched, shared copy) | – |
| upcoming notifications (scheduler) | 25.5 ms | 0.13 ms (`NotificationCache`) | 18.0 ms |
| calendar month (6 weeks) | 57 ms | 4.2 ms (`CalendarCache`) | 42.5 ms |
| query `status:open due:<=+7d sort:due` | 31 ms | 0.73 ms (`QueryCache`) | 13.8 ms |
| **all consumers** (guard row) | **112 ms** | **6.0 ms** (−95 %) | 75 ms |
| all consumers, 10 000-task vault | 40 ms | 2.8 ms | 38.5 ms |

What remains of the 6 ms is mostly the calendar range's items gathered and sorted again (the month
holds thousands of items in this vault; the changed note's are computed alone). The free functions
got faster too (full syncs fill from the index's lists, runs copy no `Task` per candidate): queries
(`bench_index:query`, 10 000 notes, the same session) `tag:praca` 78 → 35.5 ms, `type:blocks "łódź"
group:page` 74 → 33 ms, `ref:[[Strona 1]]` 89 → 49.5 ms, open due this week 28.5 → 15.8 ms, overdue
27.5 → 12.5 ms, reminders this week 9.3 → 1.5 ms, all tasks sorted 58 → 56 ms; after one edit with a
`QueryCache` (`bench_index:queryCache`): `tag:praca` 0.17 ms (runQuery 43 ms), all tasks sorted 1.9 ms
(64 ms). A perf-check run of the branch passes all 33 checks; the laptop baseline's afterEdit row is
6 ms now (was 138 ms) and its query row 36 ms (was 99 ms).

**App** (`tests/bench/app`, laptop, Release, Xvfb; the `views` scenario on the 10 000-note bench
notebook, `r0` first open, `r1` again; 5 runs per build, medians): Calendar r0 132.0 → 128.8 ms, r1
119.6 → 115.0 ms; Agenda r0 107.4 → 103.3 ms; Tasks r1 82.6 → 82.9 ms, r0 100.1 → 106.7 ms (within
the runs' spread; the query behind it is faster cold, 16–19 → 14–15 ms, and the second open is
equal); Graph, All pages and Tags unchanged. An earlier build of the branch opened the Calendar
~50 ms later (the full-sync costs above), which is why full syncs fill from the index's lists.

Equivalence is the point, so it is tested, not assumed: random edit sequences (Polish names,
renames, removals, `((id))` across notes, recurring series, malformed tokens) check at every step
that `changesSince()` names every note whose tasks, reminders, link targets or shown block texts
changed (`tst_index_consistency`), that a `QueryCache` per query equals `runQuery()` on the edited
and on a fresh index (16 queries: tags, `ref:`, `page:this`, groups, `limit:`, `remind:overdue`, …),
that `CalendarCache` / `NotificationCache` equal the free functions (a month, a week, a day, the
2026-10-25 change to winter time, windows moving across midnight, another language, a change of
the local time zone; `tst_reminders_cache`), and that the Tasks list, the calendar's overdue list (against the old scan)
and Workspace's cached lists equal what a fresh model computes. `tst_reminders_cache`
`oneEditCostsAFraction` guards the gain without timing a machine: after one edit of 3000 notes the
caches must cost ≤ 0.25 × the free functions (Debug: 2.8 vs 16.9 ms). `tst_planning_tasks`
`changeNamingManyNotesStaysCheap` guards the other end: one change naming 3001 notes (the page 3000
notes link to appears or goes) must merge into the Tasks list in ≤ 2 × the time of a list built
afresh + 5 ms (looking each changed note up among all matches took 275 vs 45 ms under the CI's
sanitizers). The Raspberry Pi was not
measured (no builds on it in this sweep): L3b in "Next steps"; it took 0.95 s before.

### Task dependencies, subtasks and clocks: kept with the index (2026-11-04)

Blocked tasks depend on other notes (docs/SYNTAX.md §5), so they follow L3's rules rather than
scanning the vault: the index keeps two compact maps — task id (`🆔`) -> notes defining it, and id
-> notes with a task waiting for it (`⛔`) — patched by `attachNote()` / `detachNote()` like the
block ids. `isTaskBlocked()` / `isTaskBlocking()` look up only the few tasks behind one id, and a
change logs, besides the note, the notes whose tasks it can block or unblock (waiting for its ids,
or defining the ids its tasks wait for — the `((id))` pattern), so `QueryCache` (`is:blocked`,
`is:blocking`), the Tasks list and every other `changesSince()` consumer evaluate just those
(`tst_index_consistency` checks it on random edits with dependencies across notes, cycles and
missing ids, and three `is:` queries against `runQuery()`). The parse adds a task's id, its
dependencies and its clock totals to `Task` (the `:LOGBOOK:` drawer is read only for blocks that
have one); summaries read the drawers of the tasks with time only (`summarizeTime()`). The subtask
counter "3/5" needs no index: the parent's own page (the editor re-renders a parent row only when
its count changes).

### Drawing backdrops: PDF renderers and what they cost (2026-10-30)

A drawing's backdrop (docs/UI.md "Drawing editor") needs PDF pages drawn as pictures on the
platforms that can afford it. Qt PDF (`QPdfDocument`, pdfium) for Qt 6.11.2, checked 2026-10-28,
and the choice per build (`src/platform/pdfpages.h`):

| build | where a renderer comes from | its size | chosen |
|---|---|---|---|
| Android arm64 | Qt PDF only as an extension of Qt's repository (`all_os/extensions/qtpdf/6112/qt6_6112_arm64_v8a`; aqt 3.3.0 does not list it for Android) | `libQt6Pdf_arm64-v8a.so` 4.53 MB (2.09 MB deflated) | the system's `android.graphics.pdf.PdfRenderer` (API 21+, min SDK 28) through JNI: no library |
| AppImage x86_64 / aarch64 | aqt `-m qtpdf` | `libQt6Pdf.so.6.11.2` 4.51 MB (archive 2.1 MB) | Qt PDF |
| Windows | `install-qt-action` `modules: qtpdf` | `Qt6Pdf.dll` ≈ 5 MB (archive 5.2 MB) | Qt PDF |
| CI Linux (ASan) | `install-qt-action` `modules: qtpdf` | — | Qt PDF (`tst_pdfpages` under the sanitizers) |
| Nix | nixpkgs has Qt PDF only inside `qt6.qtwebengine` (closure +615 MiB) | poppler-qt6 (`kdePackages.poppler` 26.09.0, GPL-2.0-or-later): closure +26 MiB | poppler-qt6 |
| browser | Qt's repository has no `qtpdf` for WebAssembly; pdf.js would add ≥ 2 MB and a JavaScript bridge | — | none: pictures only (drawings on PDF pages made elsewhere show, their pixels are in the file) |

Measured after the change (the feature's whole cost, not only the renderer):

| | before (`sweep2` 64d0380) | after | change |
|---|---:|---:|---:|
| APK arm64 release (`android.yml`, GitHub Actions, the same day) | 80 818 249 B | 81 195 081 B | +376 832 B (+0.47 %) |
| … download (every entry deflated, `size-report.py`) | 26.33 MB | 26.43 MB | +0.10 MB |
| … `libbrulion` / `classes.dex` | 31.26 MB / 493.19 KB | 31.60 MB / 496.63 KB | +0.34 MB / +3.4 KB (`PdfPages.kt`) |
| AppImage x86_64 (`appimage.yml`, `sweep2` and the branch the same day) | 55 941 624 B | 58 362 360 B | +2 420 736 B (+4.3 %): `libQt6Pdf.so.6` 4.51 MB uncompressed; QtQuick.Pdf, `libQt6PdfQuick` and the `qpdf` picture plugin pruned (−0.2 MB) |
| AppImage aarch64 (the run's artifact: the AppImage and its .zsync, zipped) | 55 592 248 B | 58 015 457 B | +2 423 209 B |
| Windows ZIP / installer (`windows.yml`, the step's sizes) | 42.9 / 29.5 MB | 45.4 / 31.3 MB | +2.5 / +1.8 MB (`Qt6Pdf.dll`; windeployqt also copies `qpdf.dll` and `qml/QtQuick/Pdf`, as it copies every style) |
| `brulion.wasm` raw / gzip -9 (`wasm.yml`; no PDF renderer: the feature's code and QML only) | 42 069 890 / 13 551 537 B | 42 274 540 / 13 606 755 B | +204 650 B (+0.49 %) / +55 218 B |
| Nix closure, aarch64 (`nix path-info -r` of the package's Qt inputs with and without poppler-qt6) | — | +25.99 MiB | poppler-qt6 6.46, poppler-data 12.38, nss 4.49, openjpeg 1.64, lcms2 0.51, nspr 0.50 MiB |

**Saving a drawing with a backdrop.** The raster is in the file twice (the scene's `files` and the
picture, as excalidraw.com's "Embed scene" export): every autosave base64-encodes and deflates it
again. `tst_drawingcanvas saveTimeWithBackdrop` logs `toSvg` of 500 strokes on a 2.5 MB PNG
backdrop and fails only above 1 s in a Release build; the backdrop is at most 4 MB
(`drawing::MaxBackdropBytes`, shrunk when larger), so the autosave stays bounded. Inserting
drawings on many PDF pages makes their files where the pages are prepared, on Qt's thread pool
(`DrawingBackdrops.prepare(…, drawings)`): 60 slides × ≈ 70–130 ms of `toSvg` on a phone no longer
run on the GUI thread after the progress bar (an Android "not responding" dialog), which only
stores the files and embeds them. Memory: the editor keeps the decoded backdrop (2400 × 1350:
13 MB) and, once shown dark, its dark twin — only of the backdrop shown: one replaced ("Another
PDF page…") is dropped and decoded again on undo; the previews decode it at the size they show
it. poppler draws a page at the resolution of the size asked for (`pdfRenderDpi`; it had a floor of
18 dpi, so a hostile 20000-pt MediaBox became a 5000 × 5000 px picture for a 230 px thumbnail).

### L16: the graph on a worker thread and the scene graph (2026-10-30)

The graph view (N5) laid its graph out on the GUI thread — a step per 16 ms timer event, on slow
devices time-boxed bursts of 10–25 ms during which input waited — and painted it with QPainter in
a `QQuickPaintedItem`, rasterised on every frame of the moving layout while the GUI thread waited
(throttled to a frame every 66–600 ms on slow devices). Now:

* **The layout runs on a worker thread** (`GraphSimulation`: one `QThread` "brulion-graph" per open
  graph, started with its first run). The GUI hands it a copy of the `ForceLayout` and keeps a
  mirror of the newest positions — what it draws, hit-tests, fits and follows —, which the worker
  publishes at most once per frame interval (16 ms); the newest frame replaces an unread one, so a
  busy GUI thread gets the newest positions, not a backlog. Commands (pin and move the dragged node,
  the drag's alpha target, reheat, pause while the view is hidden, cancel) apply before the
  worker's next step; the dragged node follows the pointer at once in the mirror. Only value
  copies cross threads (a run in, frames out; one mutex for the frame slot, one for the command
  queue). `rebuild()` (`VaultIndex::graph()` and the filters) stays on the GUI thread; the view
  sets its settings before the index, so opening it builds the graph once (before: once more for
  each setting that differs from its default — the phones' node cap, an English UI, saved
  toggles).
* **The browser** (single-threaded WebAssembly) runs the same worker on the GUI thread in slices of
  ≤ 16.5 ms per 33 ms timer event (never a 0 ms timer, N13), at least one step per slice.
* **Pacing and fast cooling instead of time-boxing.** A run unfolds over at least `minRunMs`
  (1 s: several steps per frame for small graphs; one step per frame while a node is dragged, like
  d3), a run that cannot keep up runs flat out. When the first three steps of a run cost more than
  5 ms on average it cools down in ~100 steps instead of ~300 — decided at that fixed step, so the
  layout stays deterministic: the tests compare the worker's final positions bit for bit with the
  same `ForceLayout` steps taken by hand (threaded and inline, both coolings, a pause mid-run, a
  GUI thread blocked for 300 ms). The decision no longer depends on painting.
* **Reduced motion**: the worker computes the run and publishes only its final frame; a layout from
  positions that never moved (a fresh build, also rebuilt — a filter, the local graph, an index
  change — before its first frame) shows a spinner (`GraphModel.settling`) instead of a spiral
  that jumps.
* **Drawing through the scene graph.** `GraphItem` is a `QQuickItem` with its own nodes
  (`GraphSceneNode`): edges, discs and rings as triangle meshes with a one-device-pixel fringe that
  fades to transparent (antialiasing without custom shaders: `QSGVertexColorMaterial`, so no
  shader tools in any toolchain; 16-bit indices in chunks of ≤ 65 535 vertices for GLES2), in world
  units under a camera transform; labels as `QSGTextNode`s on rounded backgrounds in screen units,
  their text layouts cached per node. Panning a settled graph only changes the camera's matrix
  (and places the labels again); a moving layout, zooming, colours and the highlight rebuild the
  meshes during the sync. No culling of discs and edges (every node goes to the GPU: ≤ 4000, 1200
  on phones), so panning never rebuilds. Both renderers place labels with one rule
  (`geometry::placeLabels`). With the software renderer (tests, screenshots, the Pi's offscreen
  runs) the QPainter code paints into an image node — the cost of the `QQuickPaintedItem` before —
  and keeps its throttling of slow frames of a moving layout (now: a paint over 16 ms).

**Unit timings** (`tst_planning_graph` `bigGraphPerformance`, 2000 nodes / 7338 edges at fit zoom;
the Raspberry Pi, Debug — the laptop was unreachable): one step 6.5 ms, building the whole graph's
meshes 3.5 ms (64 412 vertices, one chunk) against 76–78 ms for one QPainter frame — a frame of the
moving layout costs the GUI thread ~20× less, and a settled pan nothing but a matrix. Zoomed in to
3× (discs with up to 64 segments, 245 198 vertices in 4 chunks) a frame with 33 new labels took 32
ms on the Pi (Debug), and 8.6 ms on a GitHub runner (Release, OpenGL on llvmpipe; with the earlier
banded edges, 274 550 vertices) (`tst_planning_graph_sg`; on the RHI null backend — the real scene
graph without a GPU — and under Xvfb, where the OpenGL frame is also compared pixel by pixel with
the QPainter one).

**App** (`tests/bench/app`, the 10 000-note bench vault, its graph capped at 4000 nodes). The
laptop was unreachable, so both builds ran in the same GitHub Actions job (`perf-compare.yml`, `app`:
ubuntu-24.04, 4 vCPUs, Release with Qt 6.11.2 from aqt, Xvfb + Mesa llvmpipe — frames rendered on
the CPU, pessimistic; base = `sweep2` at `64d0380`, head = this change; 2 alternating rounds × 3
runs = 6 per build; medians, min–max in the summary of the run). The probe activates the window
first: under an X server without a window manager the app stays inactive and the graph's layout
paused — the `views` rows of earlier tables measured opening a graph that never laid out (it did
lay out offscreen, on the Pi). The new `graph` scenario opens the graph, drags its background for
1 s 300 ms later while the layout runs, zooms three wheel steps, waits until the layout is done,
then drags again for 1 s:

| graph, 4000 nodes (GitHub runner, llvmpipe) | before | after | |
|---|---:|---:|---:|
| open → first frame (`views` r0 / `graph`) | 1077 / 1067 ms | 122 / 132 ms | −89 % |
| longest GUI stall while it lays out (`graph` run) | 958 ms | 219 ms | −77 % |
| frame interval p95 while it lays out | 818 ms | 173 ms | −79 % |
| frames while it lays out (frames / s) | 56 in 40 s (1.4) | 70 in 9.2 s (7.6) | ×5.4 |
| layout done (with the drags and zooms) | 40.0 s | 9.2 s | −77 % |
| a frame's sync (GUI thread waits): moving layout / settled pan | 660 / 578 ms | 0.1 / 0.2 ms | −99.9 % |
| a frame's render + swap (llvmpipe) | 7 ms | 127 ms | (the meshes, on the CPU) |
| drag move → frame (median): during the layout / settled | 675 / 586 ms | 254 / 252 ms | −62 / −57 % |
| wheel zoom → frame (median) | 830 ms | 336 ms | −60 % |
| process CPU time, whole `graph` run | 131 s | 66 s | −50 % |
| RSS after the scenario | 501 MB | 557 MB | +56 MB |

The `views` scenario agrees (graph r0: longest stall 967 → 220 ms, frame p95 973 → 198 ms, settled
8.3 → 2.0 s; r1 the same; the other views unchanged within ±4 %). What is left on this machine is
llvmpipe rasterising the meshes of 4000 discs and their edges, ~127 ms per frame on 4 vCPUs, while
Qt's threaded render loop holds the GUI thread until the render thread can take the next sync; a GPU
(phones, laptops with a driver) draws the meshes in a few batched draw calls, so there the GUI
thread's own share of a frame is what is left: the 0.1–6 ms sync. That is also why the edges are
single quads (2 triangles instead of the 6 of a fringed band; an earlier run of this branch with
banded edges: `views` graph frame interval p95 321 ms against 198 ms). Of the +56 MB, about 10 MB
are ours (copies of the layout for the worker and its final frame, the meshes, cached label layouts;
an estimate), the rest most likely llvmpipe's buffers for the meshes (a GPU keeps them in its own
memory).
The Raspberry Pi (software renderer: the image node, the same work as before) and the Android
emulator were not measured (L16b).

### PDF viewer: memory, thumbnails and what they cost (2026-11-01)

The thumbnails of embedded PDFs and the in-app PDF viewer (docs/UI.md "PDF viewer") draw with the
renderers drawing backdrops brought (above: Qt PDF, poppler, Android's `PdfRenderer`); nothing new
is shipped, and the browser has no renderer (a PDF opens in a new tab there).

**The viewer's memory is bounded by the screen, not by the PDF or the zoom** (`PdfViewModel`,
`src/app/pdf/pdfviewmodel.h`):

* a page's base picture is drawn at `k = min(1, sqrt(viewport pixels / page pixels))` of its shown
  size: at most one screenful of pixels per page, at any zoom;
* zoomed in (`k < 0.98`) the visible part of each page (+64 logical px on each side, snapped to 64)
  comes at full resolution as a detail picture once the view stops moving (150 ms); together they
  cover at most the viewport;
* only the pages within half a viewport above and below the view have delegates (rows inserted and
  removed at the model's edges), and the pictures use `cache: false`, so Qt's pixmap cache keeps
  nothing that scrolled away;
* ≈ 4 × the viewport's pixels × 4 B of decoded pictures: a phone at 1080 × 2400 ≈ 41 MB, a desktop
  at 2560 × 1440 ≈ 59 MB, whatever the PDF (a 200 MB scan included) and the zoom (10–800 %);
  pdfium's and poppler's own caches come on top (the whole app on the emulator: below);
* open readers (`PdfReaderPool`): at most two idle per file and four in all, closed after 10 s
  idle, when the file changes and before Brulion itself writes, moves or deletes it; at most two
  pages drawn at once (`PdfRenderPool`: visible pages first, then detail pictures, thumbnails, page
  sizes, search last); the search is cancelled between pages when the text changes (250 ms
  debounce);
* hostile files: page aspect clamped to [1/32, 32], a page drawn larger than 32 768 px or a picture
  above 16 MP is not drawn (`MaxPdfFullSide`, `MaxPdfClipPixels`), at most 20 000 pages laid out and
  5000 hits kept.

**Thumbnails** (`PdfThumbnails`, owned by the Workspace): drawn once per file, size, mtime, page
and pixel size, off the GUI thread, at 420 logical px × the screen's ratio rounded up to 512 / 768
/ 1024 / 1280 / 1536 px, and stored as PNG files whose text chunks carry the page count and size
(the GUI thread only stats the PDF and reads a PNG's header: no decode, no PDF opened); a failure
that the same bytes give again (locked, unreadable, no pages) is stored as `<key>.err`, so a locked
or broken PDF is not opened again on every scroll; a failed drawing (out of memory) is remembered in
memory only and tried again after a restart, and a remembered thumbnail whose PNG was pruned is drawn
again. The disk
cache is capped at 64 MiB (32 MiB on Android; the oldest pruned 2 s after a store), the memory map
at 512 entries. The browser no longer builds a `data:` URL of an embedded PDF (before: a base64
copy of the whole file per embed, 1.33 × its size, for a chip that never used it).

**Timings** (`tst_pdfviewmodel timingsOfALongPdf`: a 300-page PDF with 30 lines of Polish text on every page,
written by QPdfWriter; informative — it fails only above 10 s to "Ready"):

| | Qt PDF, GitHub runner (x86_64, ASan + UBSan, -O1; one run) | poppler (nixpkgs), Raspberry Pi 4 (Brulion Debug; median of 5) |
|---|---:|---:|
| opened: "Ready" (page count, the first page's size, the layout) | 6 ms | 11 ms |
| a reader opened cold | 6 ms | 2 ms |
| page 1 drawn at a phone's fit width (1080 px) | 32 ms | 59 ms |
| a thumbnail (768 px) | 16 ms | 26 ms |
| one page searched (30 hits, their rectangles) | 264 ms | 17 ms |
| all 300 pages searched by the model (one hit a page; the 250 ms debounce included) | 4.9 s | 3.4 s |
| `tst_pdflayout manyPagesFast`: 20 000 pages laid out / 10⁶ `indexAt` | — | 1 ms / 272 ms |

The Raspberry Pi stands for a slower phone: a 300-page lecture is ready at once and a page drawn
in about 60 ms; the whole PDF is searched in about 3 s while the pages keep drawing. The search's
cost is the renderer's: Qt PDF asks pdfium for every hit's rectangles (`getSelectionAtIndex`;
264 ms for 30 hits under the sanitizers), poppler gives every character's box once per page; it
runs at the lowest priority, so pages keep drawing meanwhile, and the first hit at or after the
current page shows as soon as it is found.

**On the Android emulator** (`scripts/android-emulator-test.sh --pdf`, API 36 x86_64 image on a
GitHub runner, 1080 × 2400): TOTAL PSS 174 MB (`dumpsys meminfo`) after zooming the 3-page
`wyklad.pdf` to 800 % and scrolling through every page — the whole app, under the 350 MB budget.

**Sizes** (the feature's whole cost, against `sweep2` 422c14df built the day before):

| | before | after | change |
|---|---:|---:|---:|
| `brulion.wasm` raw / gzip -9 / brotli -q 11 (`wasm.yml`; no renderer: the code and QML only) | 42 700 155 / 13 740 047 / 9 395 509 B | 43 238 485 / 13 883 483 / 9 482 864 B | +538 330 B (+1.26 %) / +143 436 B / +87 355 B |
| APK arm64 release (`android.yml`'s artifact, zipped) | 29 450 612 B | 29 746 852 B | +296 240 B (+1.0 %) |
| Windows ZIP + installer (`windows.yml`'s artifact, zipped; Qt PDF was already shipped) | 80 169 509 B | 80 583 602 B | +414 093 B (+0.5 %) |

### Tabs, split view and windows: what a tab costs (2026-11-03)

Tabs (docs/UI.md "Tabs, split view and windows") are kept cheap by design, not by tuning:

* **Only the tabs shown have views.** The main window has two `ShellTabPane`s (the split pane
  shows nothing — no view, no Loader item — while the view is not split) and every page window one;
  a tab in the background is a `Tab` in `TabSet` — its `RouteHistory` (≤ 100 routes a side, a few
  short strings each) and a view state (two numbers). A hundred tabs (`TabSet::MaxTabs`) are a few
  tens of kilobytes; no editor, PDF reader, picture or query model stays alive for them.
* **Switching tabs is a route change in the pane.** The same view (a page after a page) keeps its
  instance and loads the other note, as a link does (the old page is saved first); another view
  (Graph after a page) is created again, as on navigation — except the bottom navigation's views,
  which the pane keeps ("Switching views" below). Coming back the pane restores
  where the tab was scrolled (`PageView.viewState()`: the first row in view and the offset into
  it, applied once the page has loaded; the journal stream: the day at the top) — the price of not
  keeping editors is one page load per switch, the same as following a link.
* **The bar** is a `ListView` of light delegates (an icon, a title, ×); `AppController.tabs` is a
  plain list rebuilt when a tab's route changes. Signals are computed by comparing a snapshot of
  the tabs before and after a change (`AppController::beginTabs` / `endTabs`): a pane hears only
  about its own tab (`tabRouteChanged(id)`), the command palette and the top bar only when the
  focused tab changed.
* **Saving** writes one JSON string per notebook (`tabs/<vault id>`, histories trimmed to 20 routes
  a side) half a second after the last change, so a burst of navigation writes the settings once.

### Switching views: the bottom navigation's views are kept (2026-11-09)

On a phone, going from Tasks to the Journal and back felt slow: `ShellRouteView` deleted the view
it left and built the next one from nothing, its models, lists and the journal's day editors
included — Tasks alone took ~0.1 s on a computer and 0.7 s on a phone (above), the Journal
about as long. Now a pane (`ShellTabPane`, `ShellRouteView.keepViews`) keeps the views of the
bottom navigation — Journal, All pages, Search, Tasks, Calendar — once it has shown them: hidden
(`visible: false`, nothing drawn or laid out), each with its own route, and shown again in the
next frame, where it was scrolled. Their cost while hidden: memory (at most five views a pane;
the journal's days are a lazy list, only the days that were on screen exist) — and, since
2026-11-18, next to nothing per edit: their models have an `active` property bound to the view's
`visible` (`WorkspaceListModel` for Journal, All pages and Search; `TaskListModel` and
`CalendarModel` for Tasks, Calendar and the side panel's day tasks; `GraphModel.shown` for the side
panel's local graph). Hidden, an index change only marks a model stale; it is computed once, at
once, when the view is shown again (before its first frame), with whatever changed meanwhile. The
first fill (built ahead) and changes of the model's own properties (a filter, the anchor) are still
computed while hidden. Shown, they compute only what changed: an autosave of a note without tasks
(before and after) costs Tasks one look at the changed notes' list and nothing else — no merge, no
layout, no row signals —; an edit of a task signals `dataChanged` only for the rows of that note's
tasks (and the rows whose section count or fold changed), not for every row; the row keys are made
once per task. The calendar skips a change of notes without calendar entries (no daily-note date,
task dates or reminders) that gave none to its range or its overdue list, and signals only the days
whose entries changed (`itemsChanged`, `revision` only when something shown changed). The weekly and
monthly notes the calendar's grids and header mark (`Workspace.periodicNoteStarts()`, a walk over
every file's name, and `periodicNote()`, a file check) are not asked while the calendar is hidden.
The side panel's local graph reads only the page's neighbourhood (outgoing links, backlinks, the
tags naming the pages, the notes carrying a #tag node) instead of the whole notebook's `graph()`,
and skips a change unless one of its notes changed, a changed note links to it, or a name changed
(`VaultIndex::changesSince()`). Until then it was not that cheap: the lists of the Journal, All pages and Search were computed
again 120 ms after every save while hidden — All pages its 10,000 rows with their backlink counts
and a sort —, Tasks and Calendar rebuilt every row, and with the views built ahead (below) that
made every autosave in a big notebook 2–3× slower ("Only what is on screen, only what changed"). Other views — a page, Graph, Habits,
Settings… — are still made per route and deleted when left, so the shell self-test's "5n"
(rounds of views and pages leave no more editors than the first round) holds. The kept views
go when the pane shows no route (the split pane closed).

**Built ahead (2026-11-10).** The first switch to one of them still built it. Now the main window's
main pane (`ShellRouteView.preloadViews`) builds the kept views it has not shown yet in the
background: 1.2 s after the notebook is ready and indexed (`Workspace.ready`, not `indexing`), one
at a time — Tasks, Calendar, Journal, All pages, Search —, the next 0.4 s after the one before has
loaded. Each is an asynchronous `Loader` (`KeptLoader.asynchronous: !shown`): the view's objects
are incubated over several frames while it stays hidden, with its plain route (`{view: "tasks"}`);
its models are filled when they are completed (one C++ step per view, not split up). Opening a view
still being built finishes it at once (asynchronous turned off completes the incubation
synchronously). A view built hidden must not take the keyboard: its `Component.onCompleted` focuses
its list or field only when it is visible, and the pane calls its `takeFocus()` the first time it
shows it (`ViewTraits.takeFocus`). The split pane and a page's own window build nothing ahead.
The shell self-test's "0b" checks that Tasks and Calendar exist, hidden, before they are opened,
and that opening Tasks shows that view with the keyboard in it. Not measured on a phone yet.

### Extensions in the notebook, and one count per page opened (2026-11-12)

Extension pages, the Luau console and `b.ai` (docs/EXTENSIONS.md) cost nothing in a notebook
without them: no VM is made until an extension or the console runs; a save looks up its path in
a hash of the extension pages (hashing the text only for those); the changed notes are looked at
after the event (`WorkspaceExtensions::processPages`, a zero timer); the one full scan — the
page-level tags of every note — runs when the index is ready. A page's state line is a `Loader`
made only on extension pages.

Measured on the 10 000-note bench notebook (`bench-matrix.sh`, Release + `-g`, Xvfb, 5 runs × 2
alternating rounds, the probe's medians), main at e1443b21 against this work:

| | before | after |
|---|---|---|
| start, first: content / ready | 855 / 1390 ms | 837 / 1379 ms |
| start, warm: content / ready | 680 / 1169 ms | 664 / 1130 ms |
| keystroke → frame (small page / 5000-block outline / document) | 13.7 / 17.9 / 20.8 ms | 13.7 / 17.8 / 21.0 ms |
| open a 5000-block document: content / frame | 179 / 196 ms | 119 / 168 ms |
| open a 5000-block outline: content / frame | 162 / 179 ms | 114 / 154 ms |
| again (open2): document / outline content | 153 / 140 ms | 107 / 93 ms |

Opening a page got faster on the way: a page that loads makes its counts available and the
stats footer shown in the same turn, and `PageView` counted the whole page for each
(`countNow()` twice, ~30 ms each on a 5000-block page) before the first frame. One deferred count
now (a `Later`) — after this change moved the second count from a half-built document to the whole
one, which made the second opening 12 % slower before the fix. The views' round
(`views-10k`) runs ~2 % more instructions on the GUI thread (the palette's five new commands are
rebuilt on every navigation); one interaction there, Agenda opened a second time, measures 30
instead of 17 ms because the same QML garbage collection now falls into it instead of just after
it (perf: the same sweep in both builds).

### Scrolling pages: cheaper rows, pages created whole (2026-11-06, 2026-11-07)

The question was whether a note's whole page could be created at once, so that scrolling it never
creates a row. Measured on the x86_64 desktop (16 cores, Release `-O3 -g`, Xvfb + Mesa llvmpipe,
60 Hz) against `da09d2d` with the app benchmarks' new `page-scroll` and `page-resize` scenarios
(tests/bench/app/README.md) and perf profiles of the scroll probe with the list's cache off.

**Pages created whole: measured on 2026-11-06, left out then, done on 2026-11-07** (asked for:
a page that never creates a row while it scrolls was worth its memory and its slower width
changes). `BlockEditor` with a cache past the end of any page of
up to 1000 rows (the rows on screen at once, the rest filled in the background by ListView's
asynchronous cache, one row per incubation slice); `run-bench.py`, 3 runs each, medians:

| | base | whole page |
|---|---:|---:|
| `Długi dokument` (300 blocks): RSS / heap after opening | 362 / 171 MB | 388 / 209 MB |
| `Długi konspekt` (~1100 rows): RSS after scrolling it | 364 MB | 562 MB |
| window 1280 ↔ 920 px, request → frame: document / outline | 36 / 36 ms | 115 / 337 ms |
| scrolling the document (wheel, 160 steps): frames > 33 ms / longest | 0 / 33 ms | 2 / 52 ms |
| opening (navigation → frame) | 87–105 ms | 86–102 ms |

The desktop had no slow frames to remove (rows cost 1–2 ms there), the background filling itself
made frames of 25–45 ms (a row's text layout is one step the incubator cannot split), each row
costs about 0.1–0.2 MB (13–25 items and a laid-out text document) and every change of the width —
the window, the side panel, a phone turned — lays out every row again. On a phone, where rows are
slower, memory is also scarcer. So rows were made cheaper first (below), and pages created whole
came a day later, with the threshold of 1000 rows (`BlockEditor.wholePageRows`; 300 on phones and
in the browser, unmeasured there) and the numbers above as their price.

**Cheaper rows.** perf (`cycles:u` at 1 kHz with DWARF stacks, scroll probe `--probe-cache 0`: every
row created at the edge of the view, down and up the page) attributed a block's creation to text
layout done several times and to the tint shapes:

* a rich-text `Text` whose height grows from 0 is laid out a second time by
  `QQuickText::geometryChange` (Qt 6.11): its height is now `Math.max(1, implicitHeight)`;
* a `Loader` reads its item's implicit width, and a wrapped `TextEdit` computes it by laying out
  its text unwrapped and wrapped again: the TextEdit of tinted text (tags, chips, inline code) now
  sits in an `Item` of its own (`BlockText`, `BRichText`);
* every tint shape (a tag's or a chip's pill) carried a `BIcon` for the rare page icon: now the
  icons have a Repeater of their own (`BTintShapes`: `RichTextPills.plainShapes` / `iconShapes`).
  (A Repeater over a count reading shapes by index would also save the model object each shape
  gets, but compiled QML in Qt 6.11 indexes no list of value types and takes none from a method
  call: its bindings would be left to the interpreter, which `tst_qml_compiled` forbids);
* the page's scroll bar (`RowScroll`, every frame while the list moves) looked the list's
  properties and methods up by name each time: now once per list and model.

Instructions counted by perf (`EVENT=instructions:u PERIOD=1000000`: a sample every million user
space instructions — neither the clock nor the other work on a shared machine changes them; timed
samples varied by up to 40 % between two runs of the same build), one run each, `--probe-cache 0`:

| | base | current | change |
|---|---:|---:|---:|
| creating a row, `Długi dokument` (paragraphs and lists with tags and links), M instructions | 8.30 | 6.27 | −24 % |
| creating a row, `Witaj w Brulionie` (tables, queries, code, callouts), M instructions | 10.78 | 8.49 | −21 % |
| `RowScroll::refresh` per frame while scrolling, k instructions: document / guide | 188 / 166 | 55 / 58 | −70 / −65 % |

The app benchmarks (`run-bench.py`, 3 runs each, medians) see what a fast desktop shows: scrolling
was already smooth (frames over 33 ms: 1–2 per 160 wheel steps in both builds, from the frames
llvmpipe draws), memory unchanged (RSS 359–361 MB), a new window width on the 300-block document
26 instead of 34 ms (no TextEdit lays its text out unwrapped for a Loader any more; the outline
30 instead of 33 ms), opening a page within the noise (80–107 vs 87–109 ms). The rows' 20–25 % matter where
creating a row is a large part of a frame: phones (a Raspberry Pi 4 needed 7–9 ms per journal
block, "Creating a day of the journal is one step" in docs/UI.md).

What is left of a row's creation is mostly QML object creation (≈ 30 %) and laying out its text
once (≈ 20 %). The rendering is unchanged (screenshots of both pages identical pixel for pixel).
Tooling fixed on the way: `scroll-probe.sh` and `perf-record.sh` opened the app on the desktop's
Wayland session instead of in Xvfb (now `QT_QPA_PLATFORM=xcb` there), `perf-record.sh` records
user space only (`EVENT`, default `cycles:u`, with `--user-callchains`: what
`kernel.perf_event_paranoid=2` allows), and the scroll probe can start sooner (`WAIT`) and label
runs of two builds (`LABEL`).

### L20: compiled QML for size, a leaner Android library (2026-11-07)

Since the whole QML was compiled to C++ (0.11.2, docs/UI.md "Compiled QML") the app's own
library grew: the arm64 APK of 0.13.1 was 105.9 MB, `libbrulion` 58.6 MB of it (38.3 MB in
0.8.1). `bloaty -d compileunits` on the unstripped library: **29.0 of 54.0 MB of its mapped size
is the C++ that qmlcachegen writes** (`.rcc/qmlcache/brulion_*_qml.cpp`, 208 files; BlockEditor
alone 1.3 MB), then 4.1 MB of relocations (`.rela.dyn`), 1.7 MB of dynamic symbols (~17 400
exported: every symbol of the static core and app libraries, `.dynsym` + `.dynstr` + the PLT) and
1.1 MB of QML source text in the resources (`qrc_brulion_raw_qml_*`), which the compiled units
make unused. Three changes (`src/app/CMakeLists.txt`):

* **The generated QML code is compiled with `-Oz`** in Release, MinSizeRel and RelWithDebInfo
  (option `BRULION_QML_CODE_FOR_SIZE`, default ON; not MSVC; `-Os` where the compiler has no
  `-Oz`). That code is glue — engine calls, property reads, conversions — whose inlined `-O3`
  copies are mostly identical. Per file (NDK clang, arm64, `.text`): SettingsView 522 → 292 KB,
  BlockEditor 980 → 665 KB; `-O2` and `-Os` gain 1–7 % only, `-Oz` wins by inlining far less and
  the AArch64 machine outliner (on at `-Oz`). Hand-written C++ keeps `-O3`: all of it at `-Os`
  would save 1.4 MB more, `-O2` 0.7 MB.
* **Android: `--exclude-libs,ALL` and RELR.** The library is opened only by Qt's loader (`main`,
  which stays exported; JNI methods go through `RegisterNatives`), so the static libraries'
  symbols are kept local (exported symbols 17 400 → 3 000, −2.4 MB). `--pack-dyn-relocs=android+relr
  --use-android-relr-tags` packs the relative relocations (4.1 MB → 28 KB of `.relr.dyn`, −4.0
  MB); Android 9–10 read them through the `DT_ANDROID_RELR` tags, 11+ as well. `--icf=safe` was
  tried and folds nothing measurable (dropped).
* **No QML source text in Android and browser releases** (`DISCARD_QML_CONTENTS` in Release and
  MinSizeRel there): those builds carry their own Qt, so the compiled units always match it and
  the source is never read (−1.1 MB). Desktop builds keep it: with a system Qt, a Qt update makes
  the compiled units stale and the engine falls back to the source.

Measured in one session on the x86_64 desktop (NDK r27c, Qt 6.11.2, emsdk 4.0.7), 0.13.1
(`2968406`) against the change:

| | base | L20 | change |
|---|---:|---:|---:|
| APK arm64 Release (`scripts/android-build.sh`, unsigned) | 105 855 511 B | 92 666 391 B | −13.19 MB (−12.5 %) |
| … `libbrulion` | 58 572 504 B | 45 381 464 B | −13.19 MB (−22.5 %) |
| … of which: generated QML `-Oz` / `--exclude-libs` / RELR / no QML source | | | −5.7 / −2.4 / −4.0 / −1.1 MB |
| APK download (every entry deflated) | 36.11 MB | 34.09 MB | −2.02 MB (−5.6 %) |
| `brulion.wasm` raw (MinSizeRel, `scripts/wasm-build.sh`) | 60 776 806 B | 54 735 094 B | −6.04 MB (−9.9 %) |
| `brulion.wasm` gzip -9 / brotli -q 11 | 18.52 / 12.15 MB | 17.14 / 11.32 MB | −1.38 / −0.83 MB (−7.4 / −6.8 %) |
| the single-file web version (`dist-wasm-single/brulion.html`) | 25 048 815 B | 23 209 411 B | −1.84 MB (−7.3 %) |
| desktop `brulion` x86_64 Release, stripped (GCC; `-Oz` only) | 78 289 280 B | 68 069 760 B | −10.22 MB (−13.1 %) |

The deflated download moves much less than the raw size: `-O3`'s copies of the same inlined
sequences compress well, and RELR's relocations were mostly zeros. The raw size is what an APK from
GitHub or F-Droid downloads (native libraries are stored uncompressed, page-aligned) and what it
takes on the device.

**Speed.** The app benchmarks on the 10 000-note vault (`bench-matrix.sh`, Xvfb, desktop Release
builds with and without `-Oz` for the generated code, alternating rounds): opening a 5000-block
page, switching pages and scrolling the journal within ±2 % (`open1_frame_ms` 189.5 vs 187.1,
`nav_frame_p95` 56.0 vs 56.8, `frame_interval_p95` 19.5 vs 19.7 ms); the views scenario swung both
ways between two sessions (+40 %, then −40 %, with the index start itself varying 3–13 s: other
builds ran on the machine). Counted user-space instructions of the whole process (`perf stat -e
instructions:u`, three warm runs each, medians): views 169.6 G vs 160.8 G, open-page 60.7 vs
46.6 G, journal-scroll 76.7 vs 79.2 G — differences in both directions within the ±10 % between
runs of one build. No slowdown was measurable; a phone's measurement is still open (L20b).

Checks: `scripts/android-emulator-test.sh` on API 36 (all checks) and on API 28 (the app starts
and draws its intro, no crash; the bridge self-test APK, which these options do not touch, fails
three Android 16-only checks there: permissions and a notification cleared by "Done");
`wasm-smoke.mjs` (`--selftest --glyphs --keyboard`, `--keyboard-phone`, `--index-notes 3000`);
the desktop Release build with `-Oz` and `BRULION_WERROR` (the whole ctest suite, 291 tests).

### L4 and L12: typing in place, a sync that reads nothing; hardened builds (2026-11-15)

Measured on the reference laptop's CPU (i5-13450HX) while other builds ran on it, so every pair
was taken alternately (base, new, base, new …) with the same `tests/bench` sources built in both
trees; the times are the ranges of those runs, the hardening costs are instruction counts
(`bench_x fn -perf -perfcounter instructions`), which do not depend on the load. The baselines
in `tests/bench/baselines/laptop.csv` are not ratcheted yet (a quiet window is needed:
`perf-check.sh -u`); the old ones pass.

**L4: `BlockDocument::setBody` for a block that is being typed in.** A body edit used to run
the whole normalisation after every keystroke: re-derive every item, renumber the lists around
the block, check the blank lines before and after it, the page's end, a snapshot of every
version for the change list — all linear in the page. Now the document remembers that the block
it normalised last is the one edited again (`m_typing`: its uid and the document's epoch, which
every other operation advances), and for it only the block's own text is replaced when the edit
kept what the rest of the page depends on: the block's kind, marker column and prefix, no
display formula opened, the separation from its neighbours, a leading rule that is still one
(`setBody`, `typingSettled`); the lines of the blocks after it are shifted when the body gained
or lost one. Anything else takes the long way as before, and so does the first edit of a block.
The two ways give the same document and the same change list: `tst_markdown_blockdoc` types
scripted and random edits into pairs of documents, one with the shortcut switched off
(`setTypingShortcut(false)`), and compares them after every step; each condition of the
shortcut was removed in turn (mutation testing) and the tests fail without it. Next to it:
`rowChanges` compares the uid lists first, `countLineBreaks` is branch-free, `toString()`
reserves, `renumberChildren` plans before it writes.

| `blockDocOps`, 5000 blocks (ms) | before | after |
|---|---|---|
| setBody (typing in one block) | 1.2 | 0.0043 |
| setBody the long way (shortcut off) | 1.2 | 0.47 |
| keystroke (setBody + version + the change list) | 1.3 | 0.12 |
| version + diff | 1.3 | 0.10–0.12 |
| toggleTask ×2 | 2.5 | 0.95 |
| indent + outdent | 2.5 | 0.96 |
| split + merge | 2.5 | 1.5 |
| insertAfter + remove | 2.5 | 1.6 |
| moveDown + moveUp | 2.4 | 1.4–1.5 |
| undo + redo | 2.0 | 1.2 |

The keystroke-to-frame budget below was not measured again: what is left of a keystroke in a
5000-block outline is the model's and Qt Quick's work.

**L12: a sync that reads nothing.** `SyncRun::readLocal` now trusts a note whose size and
modification time are those of its last sync, as it always did for binary files — but only a
*settled* one: its content was read at least `SyncFileState::SettledAfterMs` (3 s) after that
modification time (`checkedAt`, like git's racy index: a second write within the storage's
timestamp granularity must not hide behind the first). The trust only serves to conclude
"nothing to do": before a merge, an upload, a download or a deletion the note is read after
all (`syncPath`). A note dated in the future is read every time and costs no save of its state.
The other half was outside the phases: `LocalStore::reload()` parsed the whole `store.json`
(10 000 states and merge bases) again although the bytes on disk were the ones it had written;
it now keeps the bytes it read or wrote last and returns when they are the same.

| 10 000 files (ms) | before | after |
|---|---|---|
| `syncNoChanges` | 733–749 | 295–298 |
| `syncFewChanges` | 866–916 | 429–450 |
| `firstSync` | ~840 | ~840 |
| phase "transfer" of `syncNoChanges` | ~280 | 38–43 |
| `LocalStore::reload` (outside the phases) | ~250 | 0 |

The rest of a no-change sync is the listing: remote 197–229 ms (the in-memory server's XML),
local 11 ms, compare 28 ms. The Pi is still to be measured.

**Hardened builds** (`cmake/BrulionHardening.cmake`, docs/ARCHITECTURE.md "Build, test,
packaging"). Instructions per iteration of the core benchmarks, Release, GCC 16.2:

| build | against | cost |
|---|---|---|
| nixpkgs' default hardening (its compiler wrapper: stack protector, stack clash protection, `_FORTIFY_SOURCE=3`, RELRO bound at start, zeroed call-used registers) | no hardening at all | +7.8 % (geometric mean) |
| Brulion's options on top: zero-initialised locals, `-fcf-protection=full`, `_GLIBCXX_ASSERTIONS`, PIE (the rest of its list is what nixpkgs gives already; it counts in the builds outside Nix — AppImage, Windows, Android) | nixpkgs' default | +1.7 % (geometric mean; 0–4 % per benchmark) |

The one benchmark above that range was the calculator (`evaluateLongBlock` +7.2 %):
`-ftrivial-auto-var-init=zero` clears every local that is declared without an initialiser,
class objects too, and `calc::Value`'s factories declared a `Value` and then filled it in. They
now build the value in one expression through private constructors (C++ Core Guidelines C.41,
ES.22), which is also what an optimiser wants without hardening: 60.4 → 56.2 M instructions for
1000 lines, 855 → 811 M for 5000 — back to what the calculator cost before. The binaries were
checked with `readelf`: PIE, full RELRO (`BIND_NOW`), IBT and shadow stack notes, no executable
stack. The Windows and Android options are tried at configure time by their toolchains in CI.
What the packages gained from 0.21.0 to 0.21.1 — the hardening together with that version's
other changes: the APK 96.8 → 99.0 MB (+2.2 MB: branch protection, stack protectors and the
checked standard library in every function of the one big library), the AppImages 0.25 MB
(x86_64) and 0.34 MB (aarch64), the Windows installer 0.14 MB, the browser version 0.04 MB (it
is built without the options).

**The model on the device** (docs/EXTENSIONS.md "AI"; llama.cpp's libraries are built with the
same hardening). LFM3-VL 2B (Q4_0) on the i5-13450HX, 8 threads, the AVX2 + VNNI processor
plugin, `tst_ai_llama` with `BRULION_LOCAL_MODEL`: 115 tokens read in 0.52 s (some 220 a second),
answers written at about 45 tokens a second (92 in 2.0–2.1 s), a small picture with its question
read in 1.2 s. With and without the hardening options (`-DBRULION_HARDENING=OFF`) the times are
the same — under 2 % apart, the runs' own spread: the arithmetic is intrinsics over buffers, with
little for a stack protector or a zeroed local to slow down. On the Android emulator (the same
machine, the SSE4.2 plugin, 2 threads) the model opened in 2.8 s and a first "Check" — 14 tokens
read, 2 written — took 1.4 s.

On a Pixel 9 Pro (Tensor G4, 16 GB of memory; Android 17, GrapheneOS; the package built beside
the released app — docs/ANDROID.md "Signing" —, the plugin `ggml-cpu-android_armv8.6_1`,
4 threads, 16 384 tokens at once; the event log's lines, 2026-11-15) the model opens in
1.5–2.7 s, reads 145–160 tokens a second and writes 11–13. The assistant's first question read its 2 551 tokens of instructions and tools in 17.6 s,
wrote a tool call (73 tokens, 5.7 s), read the 95 tokens that were new of 2 719 in 0.9 s and
wrote the answer (104 tokens, 8.7 s): 33 s from the question to the answer. The next question
kept 2 131 of its 2 618 tokens and read the rest, a note's photo among them, in 3.6 s;
"Summarise this page" read 289 tokens in 1.8 s and wrote 237 in 19.5 s. That is two thirds of
the laptop's speed at reading and a quarter at writing.

What the runner changed after those figures (measured on the laptop — below —, and on the phone
on 2026-11-16, further down: 15–20 tokens a second written there now, against 11–13):

* **Its threads stay.** Without a pool of its own llama.cpp starts its threads for every step and
  ends them after it (`ggml_graph_compute` with no `ggml_threadpool` attached) — for every token
  of an answer, three new threads on the Pixel. Android's scheduler puts a thread that has just
  started where it costs least energy, a slow core, until it has run long enough to look busy,
  and every operation of the step waits there for it: a step of a long prompt lasts seconds and
  its threads move to the fast cores, a token's step lasts 80 ms and they hardly do — which fits
  writing at a quarter of the laptop's speed against reading at two thirds. The runner now makes
  the pool once (`ggml_threadpool_new` of the processor plugin, `llama_attach_threadpool`): its
  threads live as long as the model.
* **Spinning only where the fast cores are the model's own.** Between two steps a pool's threads
  spin before they sleep (llama.cpp's `poll` level, 50 by default: some 6.5 million rounds of
  `pause`/`yield`), and inside a step they spin at every barrier anyway. On an idle processor that
  writes 12 % faster than sleeping at once (no waking up, the cores' clocks stay up); on one that
  other programs keep busy it is ruinous: a thread spinning on a core keeps the very thread it
  waits for off it. With 16 busy loops beside it on the laptop the spinning, pinned pool did not
  finish a round in 5 minutes, the unpinned one wrote 1.5 tokens a second — against 5.9 of
  threads started for every step and 13.3 of a pool that sleeps (`poll = 0`, below). So the
  threads sleep at once (`LocalRunner::Options::spinWait` false) except on Android, where the
  system keeps background work off the fast cores (its cpusets: `background` and
  `system-background` on the slow cluster) and the app sets `spinWait`. The event log says so:
  `…, 4 threads on cores 4-7, spinning`.
* **On the fast cores.** `ai::computeCores()` reads the cores' capacities from sysfs and leaves
  out the cluster below the widest gap (Cortex-A5xx next to A7xx: a third of the speed); the pool
  and the thread that asks the model are kept on the rest (`cpumask`), one thread per core, at
  most 8. On the Pixel 9 Pro that is 4 threads, as before, but on cores 4–7 only; a phone with
  2 fast and 6 slow cores (Helio G99) now takes 2 threads instead of 4, one with 8 fast cores
  (Snapdragon 8 Elite, Dimensity 9300) 8 instead of 4. One logical processor per physical core
  (`topology/core_cpus_list`): a PC's kernel gives capacities too (1024 for each core), and
  without that rule the first 8 of the i5-13450HX's logical processors were 4 performance cores
  with their SMT siblings — reading 20 % slower (below). There the clocks tell the efficiency
  cores (3.4 against 4.6 GHz): 6 threads on cores 0,2,4,6,8,10. A kernel without capacities: half
  the logical processors, as before, in a pool of its own. The event log tells what was taken:
  `ggml-cpu-android_armv8.6_1, 4 threads on cores 4-7`.
* **The system text and the tools are read once.** After a prompt's stable part (the system text
  and the tools: 2 551 tokens of the assistant, 17.6 s on the Pixel) the runner writes the model's
  state for it — llama.cpp's cells of that sequence, 12 KB a token: some 30 MB — to
  `<cache>/model-state/<key>.state` (`QSaveFile`; at most 4 states, 192 MB together, the ones used
  longest ago go). The key is a SHA-256 of the prompt's tokens, the model's file (path, size,
  time), llama.cpp's release and its processor plugin, so a state is read back only into the
  model that computed it, and gives the very answer a fresh start gives (`tst_ai_llama
  keepsTheStartOnDisk`). The first request after the model was opened — after ten minutes
  without one, or a new start of the app — reads the file (`llama_state_seq_set_data_ext`, the
  file mapped) instead of those tokens: tens of milliseconds for 30 MB from a phone's storage
  against 17.6 s. The event log counts them apart: `… tokens read in … s (0 kept from before,
  2551 from the disk) …`. Removing a model empties the folder.

On the laptop (`tst_ai_llama speed` with `BRULION_LOCAL_SPEED=2`, `BRULION_LOCAL_SPIN=1` for
`spinWait`: 2 267 tokens of Polish text read, 256 written, each round on a model opened anew; the
variants one after the other, several series, 2026-11-15, the machine shared with other work;
"busy": 16 shell loops `while :; do :; done` beside it, one round):

| Threads | Idle: reading | Idle: writing | Busy: reading | Busy: writing |
|---|---|---|---|---|
| 8, started for every step (v0.22.0) | 204–215 | 36–41 | 131–133 | 4.4–6.5 |
| 8 in a pool, spinning, wherever the system puts them | 206–216 | 36–43 | 79 | 1.5 |
| 8 in a pool, spinning, on cores 0–7 (4 cores and their SMT siblings) | 162–166 | 33–38 | | |
| 6 in a pool, spinning, on the performance cores 0,2,…,10 (Android's way) | 200–228 | 39–45 | — (> 5 min) | — |
| **6 in a pool, asleep between steps, on the performance cores (a PC's way)** | **217–228** | **38–42** | **93** | **13.3** |
| 8 in a pool, asleep between steps, wherever the system puts them | 215 | 41–42 | 93 | 12.8 |

Tokens a second. On a PC the pool alone changes nothing (Linux puts a thread that has just
started on an idle fast core): what counts there is one thread per performance core, not two on
one core's SMT siblings, and no spinning when the processor is shared — writing twice as fast as
v0.22.0's beside a busy processor, reading at three quarters of its speed then.

**Against PocketPal, on the Pixel 9 Pro** (2026-11-16; the user found PocketPal, with the same
model, faster than Brulion's AI chat). PocketPal 1.23.0's own benchmark there — LFM3-VL-2B
Q4_K_M, 6 threads, flash attention off, 512 tokens read, 128 written —: **69 tokens a second
read, 16.5 written**. llama.cpp's `llama-bench` of the release Brulion pins (v0.9.0, built for
Android with every processor plugin, pushed to `/data/local/tmp`), Brulion's QAD-Q4_0 file, the
same 512 / 128, a pause of 40 s between runs (a phone heats up):

| Threads (llama-bench) | Plugin | Reading | Writing |
|---|---|---|---|
| **4 on cores 4–7, spinning (Brulion's)** | `android_armv8.6_1` (Brulion's) | **147–149** | **17.8–18.2** |
| 4 on cores 4–7, spinning | `android_armv9.0_1` (SVE2) | 113–128 | 16.7–24.2 (its first run, the coolest phone) |
| 4 on cores 4–7, asleep between steps (`--poll 0`) | `android_armv9.0_1` | 130 | 22.6 (against 24.2 spinning, the same series) |
| 3 on cores 5–7 | `android_armv8.6_1` | 87 | 12.6 |
| 6 wherever the system puts them (PocketPal's count) | `android_armv9.0_1` | 104 | 16.0 |
| 6 on cores 0, 1 and 4–7 | `android_armv9.0_1` | 57 | 15.4 |
| 8 | `android_armv9.0_1` | 82 | 7.3 |
| **The GPU: Mali-G715, llama.cpp's Vulkan backend, every layer** | — | **40** | **19** |

Tokens a second. Brulion's runner writes as fast as PocketPal and reads twice as fast (its Q4_0
laid out anew for the int8 matrix instructions); SVE2 brings nothing but slower reading, so the
APK keeps `armv8.6` and `armv8.2_2`. The GPU writes as fast as the processor — the same memory
bound — but reads four times slower (no integer dot product on this Mali, no matrix cores), and
the assistant reads more than it writes (2 300 tokens of instructions, search results, pages): no
Vulkan in the APK (it would add 35 MB of shaders). Its writing stays even when the processor's
does not, though: a hot phone's.

So the difference was the chat's, not the model's:

* **Nothing showed until the end.** An answer of the assistant is several requests (a search, a
  page read, the answer), each with thoughts first — 1 000 to 1 500 tokens of a question in all,
  over a minute on the phone — and the chat showed its dots until the last one ended. PocketPal
  shows every token. Now the chat does too (docs/UI.md "AI chat"): the thoughts from the first
  token, the answer as it is written, how fast (the speed over the last two seconds,
  `WorkspaceChats.speed`); the first thoughts of a question show within 0.6–1.8 s of the model's
  start (`shown from` in the event log).
* **The conversation found itself.** Its page in `AI/` holds the question word for word, so the
  first search found it, the model read it — its own question, nothing more — and searched again:
  on the laptop 8 requests and 71 s for "Co mam do zrobienia w projekcie remontu?" over the sample
  notebook, 6 and 49 s once the assistant's tools left the conversation out (`ctx.conversation`);
  without thinking (`effort = "low"`) 2 requests and 7.5 s, but an answer that missed the
  project's tasks — the thinking stays.
* **Stop did not stop the model.** The chat dropped the answer and the model wrote on to its end
  (up to 2 048 tokens: minutes on a phone), the next question waiting behind it. A stopped turn
  now ends its request (`ai::Pending::stop`: the runner's flag, a provider's connection).
* **The app's frames took the model's cores.** While the model wrote, the top bar's ✨ spinner and
  the chat's turned at the screen's rate, and Qt Quick drew the whole window anew for every frame:
  a Qt thread of the app used 55–60 % of a core all through a turn (`/proc/<pid>/task/*/stat`
  sampled over adb), beside the model's four threads on the four fast cores, which spin between
  steps and wait for the slowest at every operation — and they got 55–100 % of theirs. With the
  spinners below, the same thread uses 10 % and the model's threads 92–99 % each; the first
  request of a question wrote 253 tokens at 20.1 a second (PocketPal's benchmark: 16.5). On the laptop the same kind of load — one
  thread busy 30 % of the time on one of the model's cores (`uiload.py`, 5 ms of every 16) —
  took writing from 40 to 30 tokens a second asleep between steps and from 48 to 38 spinning;
  one busy 6 % (1 ms of 16) from 40 to 37 and 48 to 45. The two spinners now turn in steps ten
  times a second (`BSpinner.calm`), the chat's is hidden while words come, and the words are
  shown ten times a second at most. Then, with Qt 6.12 (docs/ARCHITECTURE.md), only the content's
  own frames are left: the ✨ stands still while a chat view shows the turn
  (`WorkspaceChats.runningShown`), the answer is two texts — its finished blocks rendered once,
  the block being written alone at every change (Qt's documentation: "Calculating text layouts
  can be a slow operation") — and that block and the thoughts have `mutabilityGroup:
  Item.DynamicMutabilityGroup`, so the renderer does not upload the rest of the window's text
  again with them. Frames drawn while a streamed answer comes (`qt.scenegraph.time.renderloop`
  counted, the app offscreen against a server that streams 15 pieces a second): 0.23.0 60 a
  second (the spinners at the screen's rate), the spinners in steps 19 while the model thinks and
  16 while it writes, with these changes 9 and 11 — the words' own updates.
* **A hot phone.** Charging over USB after twenty minutes of benchmarks, the Pixel capped its X4
  core at 0.7–1.4 GHz (of 3.1) and the A720s at 1.8 GHz (of 2.6), thermal status 1, the skin at
  39 °C: the model wrote 1.2–7 tokens a second in the app — a long turn's context counts too:
  llama-bench on the hot phone wrote 14.9 tokens a second at no depth, 11.9 after 4 096 tokens,
  9.3 after 8 192 — and a request that wrote 710 tokens took 280 s. Leaving the capped X4 out
  (3 threads on the A720s) was no clear gain there: 11.9–13.7 against 13.4 with it, steadier
  though. A phone that is charging heats up within a minute or two of answering. Five minutes for the whole
  request ended such a turn as "no answer"; the limit is now five minutes without progress (no
  part of the request read, no token written: `LocalRunner::Params::timeoutMs`), and a request
  the chat streams from a provider on the network ends only after five minutes without data
  (`HttpRequest::timeoutWhileIdle`).
* **A small model goes round in circles.** On the phone the same question took the model 14
  requests in ten minutes (searches, queries and pages again and again, its context growing to
  9 600 tokens with every result kept: a result is up to 12 000 characters) where the laptop's
  took 6 — a fact of a 2B model, not of the runner; shorter tool results and fewer tools for a
  small model are in TODO.md.

Its memory (the app of the Nix package, 16 384 tokens at once, "Check" pressed): the process
grows from 201 to 2 504 MiB resident — 1.3 GiB of its own and 1.0 GiB of the model's file. Its
own: llama.cpp lays the Q4_0 weights out anew for the processor's instructions (787 MiB, no
longer the file's pages), the picture part is read in (194 MiB), the context's cells take 12 KB
a token (202 MiB for 16 384) and the work buffers the rest. Of the file's mapped pages only the
205 MiB not laid out anew are read again; the others the system takes back when it wants
memory. The model opens in 0.5 s while its file is in the system's cache.

On the Pixel (`dumpsys meminfo`, PSS): the app 472 MiB before, 2 913 MiB with the model — 1.45 GiB
of its own (0.15 GiB more than on the laptop) and 0.98 GiB of the file's pages —, 3 029 MiB
after the photo was read. After 45 s behind another app the model was still
there and answered at once; ten minutes after its last answer it left, and the app was back at
461 MiB.

Its download (src/core/ai/localfiles.cpp) holds 4 MB read ahead, over HTTP/1.1. Without that a
thread that does not read for six seconds — as an Android app's does while it is off the screen —
had a 3 GB file wholly in memory: 3.2 GiB resident over HTTP/1.1 (6.6 GiB with TLS), and 3.2 GiB
over HTTP/2 with the limit or without, because there Qt does not make the server wait. With the
limit over HTTP/1.1 and TLS the process stayed at 38 MiB (a small program with Qt 6.11.2
against servers on the same machine, 2026-11-15). `tst_ai_localfiles` keeps it so: after a
second without reading, its server still holds 54 of the 64 MB it has to send.

**NFC in linear time.** `QString::normalized(NormalizationForm_C)` sorts combining marks with an
insertion sort and decomposes in place: 16 000 marks above a letter followed by 16 000 below
take it 4.1 s, 160 000 letters after one decomposed pair 3.7–4 s, and four times that for twice
the text — a file name or a link in a note that came from anywhere could stop the app for hours.
`unicode::nfc` (`util/unicode.h`) returns ASCII as it is, hands short text to Qt, and
normalises long text in pieces that start where no composition reaches across (a starter that
is no Hangul vowel or final); a piece with more than 64 marks stays as written. 400 000
characters of each of those texts take 1–23 ms (`tst_unicode`, which also checks that every
text Qt normalises in reasonable time comes out the same).

### Source mode: one edit, not the page (2026-11-16)

**Compared with QOwnNotes.** QOwnNotes (github.com/pbek/QOwnNotes at 120ee350, its editor
qmarkdowntextedit, the changelog's speed entries since 2017) is a Qt Widgets notes app that keeps
a whole note in one `QPlainTextEdit`. What it does for speed, and where Brulion stands:

| QOwnNotes | Brulion |
|---|---|
| Edits of the note followed through `QTextDocument::contentsChange` instead of `toPlainText()` on every keystroke (its Markdown language servers, 26.4.25) | **missing in source mode** — below |
| The preview's HTML built only while the preview is shown, cached, rebuilt only for a changed text | blocks rendered by `InlineRenderer` when shown, rows cached ("Scrolling pages" above) |
| Find in the note debounced on notes over 20 000 characters, from two characters on | `NoteFind`: 120 ms, 400 ms over 200 KB; one character searches (Polish one-letter words) |
| Highlighter: hand-written scanners over `QStringView`, few static regexes, a format run set once | `MarkdownHighlighter` on the markdown module's scanners, static regexes, `mergeRange` per run |
| Settings read through an in-memory cache (24.9.7) | `AppSettings` properties; `QSettings` only at start and in platform code |
| Note relations gathered on a thread, dropped for another note | the graph laid out on a worker (L16) |
| Backlinks not scanned again while typing when nothing they depend on changed (26.6.4) | `VaultIndex` follows changes (L3) |
| Remote pictures cached after their download | `webpicture`, the device's picture cache |
| Notes in an in-memory SQLite database | `VaultIndex`, `SearchIndex` (BM25+), the index cache (L2) |
| Precompiled headers in developer builds (`pch.h`) | ccache only — a possible gain for cold builds (the Pi, CI after a Qt update), not tried |

**Source mode handed the whole page over on every keystroke.** `SourceEditor.qml` copied the
TextEdit's whole text (`EditorInput.plainText`) on each `textChanged`, compared it with the last
copy and gave it to `PageController::setSourceText`, which mapped its line breaks back onto the
file's (`restoreLineEndings`: a copy of the text plus a `QList<qsizetype>` of every character's
origin, 8 bytes a character) and compared again. A TextEdit also reports a change of formats as
`textChanged` (each edit sets its lines' heights), which paid for the copy and the comparison
once more. Now `SourceTextSync` (`src/app/editor/sourcetextsync.h`) takes the change from
`QTextDocument::contentsChange` — its position and added count; the removed count from the
document's lengths, because a change that reaches the end counts the last paragraph separator in
both — and reads only the added characters (`QTextCursor::selectedText`).
`PageController::editSourceText` places it in the file's text with `sourceEditOf`
(`blocktext.h`): from one `\r` to the next up to the edit, the reported range narrowed to what
differs (a change of formats replaces nothing, line terminators inside a wide range keep their
bytes), a lone `\r` never merged with a `\n` typed after it (`restoreLineEndings` merged them, so
such files showed one line break less after a save).

Measured on the Raspberry Pi 4 (Qt 6.12.0, `-O2`, a `QTextDocument` and both ways' functions
copied from the sources, 50 keystrokes in the middle of a page of Polish list lines, three runs):

| page | whole text: a keystroke | whole text: a change of formats | one edit: a keystroke | one edit: a change of formats |
|---|---|---|---|---|
| 20 KB | 0.68–1.07 ms | 0.08–0.10 ms | 0.016–0.024 ms | 0.012–0.020 ms |
| 100 KB | 3.6–4.1 ms | 0.49–0.58 ms | 0.056–0.10 ms | 0.034–0.061 ms |
| 500 KB (LF) | 18.0–18.6 ms | 3.06–3.39 ms | 0.52 ms | 0.19–0.20 ms |
| 500 KB (CRLF) | 18.9–21.4 ms | 3.09–3.42 ms | 0.63 ms | 0.25 ms |

What is left grows with the page, but at memory speed: `QString::replace` moves the text after
the edit, and the walk to the edit searches for `\r` up to it. The highlighter's and the
TextEdit's own work per keystroke is the same both ways and not in these numbers.
`tst_editor_sourcesync` times both ways on a 500 KB page in one process
(`keystrokeCostsTheEditNotThePage`, with and without the highlighter), each by what it does
itself — the sync's slot between two slots that read a clock, the whole text's copy, comparison
and `setSourceText` — not the `QTextDocument`'s own work (insertion, layout, highlighting,
3.2–3.9 ms a keystroke there in a Debug build, the same both ways; a first version subtracted it
and failed on a busy CI runner). It fails when they are less than 3× apart (Debug, x86_64: about
0.1 ms by edits against 4.2 ms by whole texts; 24 copies at once on 16 cores: at least 21×).

**In the app** (`tests/bench/app`, scenario `source-typing`: the page opened, source mode on, the
caret at the end of the middle line, three bursts of 19 keys 120 ms apart, five Enter + a letter,
five Backspaces; the reference laptop, Release, Xvfb, 10 runs per build in two alternating rounds;
medians of the runs' medians, min–max in brackets; base = `6e4e15f2`, the commit before):

| `source-typing-outline5000` (`Bench/Konspekt 5000.md`, 0.5 MB) | before | after |
|---|---:|---:|
| letter: handling | 6.0 ms [5.3–6.6] | 1.6 ms [1.5–1.7] |
| letter: handling, p95 | 20.7 ms | 1.9 ms |
| letter → frame / p95 | 12.6 / 26.8 ms | 11.5 / 12.6 ms |
| Enter: handling / → frame | 20.1 / 25.7 ms | 1.8 / 11.3 ms |
| Backspace: handling / → frame | 18.0 / 23.5 ms | 1.6 / 11.6 ms |
| GUI blocked after a burst (the autosave) / its longest stall | 315 / 97 ms | 315 / 98 ms |
| CPU time of the whole run | 15.5 s | 14.7 s |

`source-typing-medium` (`Bench/Średnia strona.md`, 32 KB): a letter's handling 0.8 → 0.6 ms
(p95 16.1 → 0.7 ms, → frame p95 22.7 → 12.4 ms), Enter 0.8 → 0.5 ms, Backspace 0.7 → 0.4 ms
(p95 15.0 → 0.6 ms); the autosave the same (250 / 241 ms blocked). Every key now shows in the
next frame (llvmpipe's ~11 ms frame); before, a keystroke paid for the whole page (and again
for the change of highlighting it caused), and on the long page Enter and Backspace missed a
frame. What a burst's autosave blocks in source mode (about twice what it blocks in the outline;
not looked into yet) is unchanged: TODO.md.

The fuzzing that came first (a standalone program: random typing, pastes with `\r\n` and `\r`,
deletions across lines, undo and redo, edit blocks, select-all, character and block formats and a
highlighter on documents with LF, CRLF, mixed and lone-`\r` endings; ~150 000 changes per run)
found no change the TextEdit and the controller saw differently, and the first version's one
fault: without the narrowing a change of formats over a line with another terminator than the
document's rewrote it (67 times in one run, 0 since). `tst_editor_sourcesync` keeps that
(`randomEditsKeepTheTextInStep`), `tst_editor_blocktext` the placing (`sourceEditOf`, 3000
random edits of random files against what the TextEdit shows).

The safety net: an edit the controller cannot place (`SourceEditResult::Unfit`) or refuses, or
another change of its text while an edit is handed over, switches the session back to whole
texts — the old way. After 250 ms without an edit (`SourceTextSync::VerifyDelayMs`, before the
400 ms autosave) and whenever the controller is about to save or leave source mode
(`PageController::sourceTextWanted`), the two texts are compared once; a difference (none is
known) is logged, the TextEdit's text taken, and the session goes on with whole texts.

### Only what is on screen, only what changed (2026-11-18)

Why Brulion did not feel as smooth as Zed while typing in a big notebook. Zed's editor is not
faster at drawing — Qt's scene graph draws as fast (the graph since L16) — but it follows four
rules everywhere: (1) only what is on screen is computed and laid out, (2) a change notifies
only what depends on it (per-entity `notify`/events, not a global counter), (3) heavy work —
writing the file, parsing — runs on a background executor over a cheap snapshot, the main
thread only merges the result (it waits at most ~1 ms for it, `Buffer::reparse`), and (4) the
editor is one element that lays out and paints only the visible lines (`DisplayMap`,
`LineLayoutCache`). Zed's save (`Worktree::write_file`) runs in `background_spawn`, without
fsync, and records the new mtime before the watcher's echo arrives, so its own write produces no
event (read in Zed's source at df5cf7b7). Brulion broke all four rules after each autosave.

**What an autosave cost (attribution).** A perf profile of main (c8bc2135) on a GitHub runner
(Release `-g1`, Xvfb, the 10k vault; the workflow was a throw-away branch) sliced to the GUI
thread's samples in the 1.7 s after each typing burst: about 80 ms per save on the small page,
about 100 ms on the 5000-block outline. Of that, inclusive:

| what (GUI thread, after a save) | small page | 5000-block outline |
|---|---|---|
| list models refreshing (`WorkspaceListModel::refresh`) | 66 % | 57 % |
| … All pages' sort (`PageListModel::sortPages`, a hidden view) | 27 % | 19 % |
| … the sidebar's folder tree rebuilt and sorted (`FolderTreeModel::rebuildChildren`) | 23 % | 17 % |
| … of those two: `compareNames` (NFC + lower case of both names per comparison) | 44 % | 32 % |
| the save itself (`PageController::save` → vault → `onVaultChanged`, index parse) | ~11 % | 20 % |
| QML bindings re-evaluated (`QQmlBinding::update`) | 14 % | 15 % |

fsync was not the problem there (15 `fdatasync` of 13 ms in all over the run, strace), and a
desktop's disk rarely is; on a phone's flash it can be (below). Most of the cost came from views
the user did not see: since "Built ahead" (2026-11-10) the Journal, All pages and Search exist
hidden from the start, and their lists were computed again after every change.

**What changed (step 1).**

* `WorkspaceListModel.active` (QML: `active: root.visible` in AllPagesView, SearchView,
  JournalsView): while it is false a change of the files or the index only marks the rows stale;
  they are computed synchronously when the model is active again, so the view shows them in its
  first frame. The first fill, a new workspace and changes of the model's own properties are
  computed as before. A date route given to the hidden Journal scrolls once its days are computed
  again.
* `WorkspaceListModel::affectedBy(IndexChanges)`: the index's change log since the last refresh
  (`VaultIndex::changesSince`) decides whether a change can concern the rows; a complete log
  without changed names that touches none of them is skipped. `GroupedRefsModel` knows the notes
  its groups come from, `BacklinksModel::mayNowReference` the notes that may now add a row
  (linked: a link to the page or a tag naming it; unlinked: any other note), and the rows remember
  what their rendering read from other notes (a linked page's icon, the text of a `[[Page#^id]]`
  block) and compute again only when that differs. Typing in a page no longer looks for its linked
  references or its unlinked mentions (a scan of the whole notebook's text) again.
* `FolderTreeModel` follows `Workspace::fileListChanged` (a file or folder created, deleted or
  renamed) instead of every `filesChanged`: its rows depend on paths only.
* `FolderTreeModel` and `PageListModel` sort with each name's key computed once
  (`text::nameSortKey` + `compareNameKeys`, the same order as `compareNames`).

**Measured** (`bench-matrix.sh typing`, the 10k vault, Release `-g1` of c8bc2135 and of the
branch, Xvfb + llvmpipe on the 16-core x86_64 desktop with other agents' work on it, 5 runs each,
median [min–max]; `autosave_blocked` sums the GUI stalls > 12 ms in the 1.7 s after a burst,
`autosave_stall_max` is the longest of them):

| typing (ms) | base | branch |
|---|---|---|
| small page: autosave blocked | 239 [224–248] | **97** [84–193] |
| small page: longest stall | 99 [95–99] | **51** [43–92] |
| small page: key → frame, median / p95 | 8.8 / 10.9 | 9.0 / 12.6 (noise) |
| 5000-block outline: autosave blocked | 663 [380–1162] | **243** [227–292] |
| 5000-block outline: longest stall | 198 [123–465] | **134** [105–153] |
| 5000-block outline: key → frame, median / p95 | 16.4 / 20.2 | **12.9 / 16.7** |
| 5000-block document: autosave blocked | 649 [477–782] | **222** [178–385] |
| 5000-block document: longest stall | 236 [152–307] | **132** [109–219] |
| 5000-block document: key → frame, median / p95 | 33.4 / 69.3 | **16.8 / 20.3** |

The base numbers are 3–5× those of the budget table (41 ms on the small page): those were taken
before the views were built ahead. A run's first burst may start before every kept view exists
(the measurement does not control it yet: Z9 below).

**Step 2: Z2–Z9 (the same day).** Ten analyses of the code (the table below) became seven
branches, merged here: the vault's own write (Z2: one read of the file, the hash computed once,
the sorted file lists patched in place, the echo of an own write recognised by its folder's
nanosecond stamp without a listing — `bench_vault:ownSaveEcho`, 10,000 notes in one folder:
echo 159 → 0.009 ms, write 55 → 4.7 ms), narrow revisions (Z4: `Workspace.namesRevision`,
`propertiesRevision`; `PageTools` only for what its summary read), hidden Tasks, Calendar and the
side panel's local graph (Z5), the editor's re-rendering after an index change (Z3:
`BlockListModel::followIndex`, `PageController.active`; Z7) and its keystroke (Z6: "Typing: a
keystroke costs the block" below), the index cache and the search index off the GUI thread (Z8:
"No more hitches a minute after typing" below), and the probe's attribution of a save (Z9: signal
spans, fsync, instructions — tests/bench/app/README.md "Autosave: where the GUI thread's time
goes"). Three reviews of the merged code (data safety of the vault, the editor, stale data in the
models) found eight smaller faults, fixed with tests; one more came from the measurement itself
(a page's links to itself made its own edits compute its backlinks: `mayNowReference`).

Measured with that attribution (`bench-matrix.sh typing`, the 10k vault, Release `-g1`,
`BENCH_WAIT_KEPT=1` — all five kept views exist before the first burst —, 4 runs per build in two
alternating rounds; main = c8bc2135, step 1 = 0f6bab95, all = the merged branch, the 5000-block
pages again after the self-link fix, 3 runs; instruction counts are user-space instructions of the
GUI thread per save, spread < 1 %, the milliseconds are wall time on a machine loaded by other
agents' builds, load 15–25):

| per autosave (median) | main | step 1 | all |
|---|---|---|---|
| small page: instructions, the save itself / what follows it | 352 M / 2406 M | 352 M / 828 M | **1.0 M / 4.6 M** |
| small page: GUI CPU, the save / what follows | 85 / 488 ms | 82 / 184 ms | **1.8 / 3 ms** |
| small page: GUI blocked after a burst | 635 ms | 254 ms | **11 ms** |
| small page: rows signalled changed | 12,655 | 11,480 | **0** |
| 5000-block outline: instructions, save / what follows | 724 M / 3107 M | 731 M / 1534 M | **220 M / 361 M** |
| 5000-block outline: GUI blocked after a burst | 740 ms | 425 ms | **69 ms** |
| 5000-block document: instructions, save / what follows | 795 M / 2972 M | 802 M / 1370 M | **258 M / 434 M** |
| 5000-block document: GUI blocked after a burst | 804 ms | 352 ms | **89 ms** |
| 5000-block document: key → frame, median / p95 | 41 / 73 ms | 32 / 54 ms | **23 / 35 ms** (loaded machine) |

On the small page an autosave now costs the GUI thread about 2 ms of CPU (the fsync of the
write, 3–5 ms of wall time, waits on top of it); a burst's 1.7 s afterwards hold 3 ms of work.
What is left on the 5000-block pages is the save itself: `Vault.changed` — the note parsed again
and merged into the index, ~40–46 ms — and the write with its read-compare, ~12–18 ms; that is Z1,
the save on a writer thread (next section: 0.8–1 ms of it left on the GUI thread).

**What is left — ten analyses of the code (2026-11-18).** Read from the code, not measured one by
one; the estimates are for the 10k notebook on the desktop (a phone: ×4–6).

| # | where | what still runs per save (or per key) | fix |
|---|---|---|---|
| Z1 (**done**, below: "The save on a writer thread") | `Vault::writeTextIfUnchanged` → `writeFile` → `onVaultChanged` → `indexFile` | the whole save on the GUI thread: the file read 4 times, hashed 6–7 times, decoded 3 times; `QSaveFile::commit` (fdatasync); the History snapshot (3 fsyncs when not throttled, and the history index JSON read every time); the note parsed again (20–50 ms at 500 KB) | first without threads: pass the bytes and the hash through, index the text just written; then Zed's way — a per-vault serial worker does read-compare, History, atomic write, stat and `VaultIndex::prepare`, the GUI thread merges (`adoptRead`/`updatePrepared` exist); every other vault operation on that path waits for the write in flight; `PageController` keeps a save generation, `flush()` stays synchronous for navigation, close and Android's pause; inline on WebAssembly |
| Z2 | `vaultscan.cpp` `processPendingEvents` → `reconcile` | the watcher's echo of our own write, 50 ms later: the folder listed (a stat per entry) and every known file walked (`verifyListing`, `diff`), 1–50 ms, much more on Android's FUSE storage; plus a full rescan every 120 s and on every return to the app | the parent folder's stat recorded after our write skips the listing; diff only the listed folder's children; rescans listed on a worker |
| Z3 | `BlockListModel::invalidateRendering` (every live editor, the hidden Journal's days too) | after every index change every row's HTML, embeds, tables and charts dropped and `dataChanged` over all rows (5–40 ms) | invalidate only rows whose rendering read a changed note (`changesSince`), `PageController.active` for hidden editors |
| Z4 | `Workspace.revision` in QML (~30 bindings) and `Vault::files()` | `files()` copied and re-sorted after every write (`trackFile` drops the sorted list); hidden All pages' `subfolders` (twice), the calendar grids' `periodicNoteStarts`, `PageTools::refreshSummary` (a regex over the page, all tags twice) — 10–25 ms | narrow notifications (`namesRevision`, `propertiesRevision`), visibility first in hidden bindings, the sorted list patched in place |
| Z5 | `TaskListModel`, `CalendarModel`, the local graph | hidden Tasks and Calendar rebuild 120–150 ms after every change (whole-list passes, `dataChanged` over all rows, a 24-key map per calendar item); the side panel's local graph builds `VaultIndex::graph()` of the whole notebook for depth 1 | `active` as above, nothing when the changed notes have no dated items, per-row/per-day changes, a neighbourhood graph |
| Z6 | the keystroke | O(page) per key: `markDirty()` serialises the page (`textToSave`), `BlockListModel::apply` rebuilds its index and walks every row, `OutlineModel` rebuilds on any `dataChanged`, the edited block's hidden rich text re-renders and lays out on every key, two completion detects per key | freeze the edited row's HTML, dirty by the edited block, a typing fast path in `apply` (cross-checked against the long way), narrow roles, `OutlineModel` only for headings |
| Z7 | `SuggestionModel` | after one `[[` or `((` completion every save ranks all 10k names / sorts all blocks again, per editor (the mode is never reset on close) | reset on close, refresh only while open |
| Z8 | `IndexCacheKeeper`, the search index | 60 s after every pause the whole 19 MB index cache is encoded in 8 ms slices on the GUI thread (122 ms at 10k; 0.5–1 s of dropped frames on a phone); a changed note is tokenised whole on the GUI thread 300 ms after a save (22 ms at 500 KB) | write when the app goes inactive or closes (and a long cap), ≤ 4 ms slices; `SearchIndex::prepare` on a worker |
| Z9 | the measurement | `autosave_blocked` misses work under 12 ms (the 8 ms aim is invisible), has no phases, and does not know whether the kept views exist | probe spans of the save's signals and timers (`qt_register_signal_spy_callbacks`), fsync/rename interposed, CPU and instructions per stall, `kept_views`, `BENCH_WAIT_KEPT`; a core `saveRoundTrip` row with a scaling limit (a save is O(1) in the notebook's size) |
| Z10 | the editor itself (Zed's rule 4) | each row is a QML delegate with a rich-text `QTextDocument` (0.1–0.2 MB a row, ~45 objects + ~25 components); pages up to 1000 rows are created whole, so a new width lays every row out (337 ms on an 1100-row outline) | in stages: (A) a C++ item drawing a block's rendered text from a cached `QTextLayout` through `QSGTextNode` instead of rich `Text`/`TextEdit` (runs from `InlineRenderer` instead of HTML; done behind `BRULION_TEXT_ITEM=1` on 2026-11-18, below: "Z10 stage A"); (A2) a journal day as one item; (C) one item per page with a height sum tree, anchor scrolling and only the visible rows laid out, rich blocks hosted as QML delegates; (B) the edited row last, for cross-block selection rather than speed |

Also found: on Android the GitHub change list (`localChanges`, 700 ms after a save) walks every
file when a repository is linked, the home-screen widgets are refreshed through JNI 2 s after each
pause even when nothing they show changed, `store.json` is rewritten whole for any small UI-state
write, and the QML garbage collector runs whole collections (`QV4_GC_TIMELIMIT=0`, a workaround
for an incremental-GC bug in Qt 6.11.2 — keep it; collect while idle instead).

**Rejected: a new renderer** (SDL3 GPU or our own `QRhi` glyph atlas). SDL_gpu has only Vulkan,
Metal and Direct3D 12 back ends — no GLES (older Android devices), no browser (its WebGPU back end
is an open pull request) — and gives no text shaping, IME, accessibility or widgets; Qt's RHI
already drives Vulkan, Metal, Direct3D and GL/GLES/WebGL under the scene graph. The time goes to
work on the GUI thread, not to drawing; Zed's speed comes from the rules above, which Qt allows.

### The save on a writer thread (Z1, 2026-11-18)

Zed's save (rule 3 above): the GUI thread only takes the text to save — an implicitly shared
`QString`, O(1) — and merges the result. `PageController`'s autosave timer calls
`saveInBackground()` → `EditorHost::writeIfUnchangedAsync()` → `Workspace` (the extensions' "save"
hooks first, as before) → `Vault::writeTextIfUnchangedAsync()`, whose work runs on a writer of the
notebook's own (a `QThreadPool` of one thread at normal priority, not the index pool): the
compare-and-swap read of the exact bytes, the History snapshot (`History` is thread-safe now), the
atomic write with fsync, the stat, the hash, and `VaultIndex::prepare()` of the text written. Back on
the GUI thread the vault adopts the result (`trackFile`, `rememberContent`, the folder's stamp for
the echo), the editor learns its new merge base, `changed()` reports the write, and the index takes
the prepared note (`updatePrepared`, no read, no parse); the editor recognises the echo of its own
write without reading the page (`Vault::holdsText`: the very string written). Every other vault
call that reads or changes files waits for the writes in flight and merges them first, a watcher
batch waits for them (at most 2 s), and `save()` — navigation, closing, Android's pause — waits for
its editor's write and then writes synchronously (docs/ARCHITECTURE.md "Vault threading" has the
rules). WebAssembly and `BRULION_INDEX_THREADS=0` run the same job at once.

The atomic write itself changed with it (a data-safety review of the echo shortcut, 630d6cde):
`vaultutil::saveAtomicallyStamped` writes and syncs its own temporary file and stamps the folder
right after creating it and right before the rename, so only those two system calls are trusted
to be the folder's only changes — another program's change while the data is synced (Syncthing
renaming its version of another note over it) is seen and listed; a kernel whose file times follow
its clock tick (Linux before 6.13, Android) shows itself at the first saves and lists every echo;
the own write is noted only after the file is tracked; History's memo takes the stamp of the index
file it wrote, from that file before its rename.

**Measured** (`bench-matrix.sh typing`, the 10k vault, Release builds — `BRULION_LOCAL_AI=OFF` for
both — of 8e678649 and of the branch, Xvfb + llvmpipe on the 16-core x86_64 desktop, load 4–9,
3 runs × 2 alternating rounds, median [min–max]; the probe's per-phase metrics,
tests/bench/app/README.md "Autosave: where the GUI thread's time goes"; `save_merge_*` is the
merge of the result from the event loop, new with this change):

| autosave on the GUI thread | base | branch |
|---|---|---|
| small page: the save (`save_sync_ms`) | 15.5 [14.7–17.5] ms | **0.2** [0.2–0.2] ms |
| … of it fsync (`save_fsync_ms`) | 14.7 ms | 0 (on the writer) |
| … CPU / instructions (`save_cpu_ms`, `save_insns_M`) | 1.1 ms / 1.0 M | 0.2 ms / 0.2 M |
| … the merge (`save_merge_ms`) | — | 0.2 ms |
| small page: longest stall (`autosave_stall_max`) | 17.3 [15.7–18.7] ms | **0** (none over 12 ms) |
| 5000-block outline: the save | 53.5 [46.6–65.3] ms | **1.0** [0.9–1.1] ms |
| … CPU / instructions | 30.1 ms / 222.6 M | 0.9 ms / 3.9 M |
| … the merge (CPU / instructions) | — | 9.1 ms (9.0 / 64.7 M) |
| 5000-block outline: longest stall | 65.8 [58.6–78.4] ms | **31.0** [30.4–31.5] ms |
| 5000-block outline: blocked in all (`autosave_blocked`) | 97.4 ms | 53.3 ms |
| 5000-block document: the save | 57.8 [50.9–64.9] ms | **0.8** [0.7–0.9] ms |
| … CPU / instructions | 35.2 ms / 262.1 M | 0.8 ms / 3.4 M |
| … the merge (CPU / instructions) | — | 11.2 ms (11.2 / 83.9 M) |
| 5000-block document: longest stall | 72.5 [66.0–79.7] ms | **26.8** [15.1–29.6] ms |
| key → frame, median / p95 (all three) | 9.0–10.7 / 11.1–13.4 ms | the same |

What the GUI thread still does after a save of a big page: the merge's `Vault.changed` (9–11 ms
at 5000 blocks: the index's patch of its derived lists with the note, the direct fan-out of
`fileChanged`/`indexChanged`) and, 200 ms later, a list's refresh
(`timer:200ms(WorkspaceListModel.refreshed)`, ~29 ms a save: the longest stall in these runs) —
the page's own linked references, computed again because the page links to itself
(`BacklinksModel::mayNowReference` counted those links; fixed on the branch since, "Step 2" above).

Since merged: the temporary file's sync comes from the platform layer (`vault/filesync.h`, set by
`platform::installFileSync()`: fsync, Windows' `_commit`; core takes no platform `#if`; without one,
as in brulion-cli, the writes are QSaveFile's and the echo lists the folder). A review of the
merged change found and fixed: `Vault::stat()` / `exists()` of a path being written now wait for
that write (a GitHub pull's or a sync's "unchanged since the plan" guard read the stat before the
editor's write and then wrote the server's version over it), the own write refuses a read-only
note as QSaveFile does and tries a refused rename again, a failed background save is tried again
after 2, 4, 8 … s, and a write dropped because its notebook closed reports Failed.

**All of it together** (the branch's head after Z1 and the reviews' fixes against main c8bc2135;
`bench-matrix.sh typing`, the 10k vault, Release `-g1`, `BENCH_WAIT_KEPT=1`, 2 runs × 2 alternating
rounds, load 5–15; GUI thread per autosave, median [min–max]; instructions in user space, spread
< 1 %):

| per autosave | main | the branch |
|---|---|---|
| small page: GUI blocked after a burst | 325 ms | **0** (no stall over 12 ms) |
| small page: the save / its merge / what follows (instructions) | 352 M / — / 2409 M | **0.2 M / 0.3 M / 5.1 M** |
| small page: the save on the GUI thread | 61 ms | **0.4 ms** |
| 5000-block outline: GUI blocked / longest stall | 373 / 113 ms | **24 / 22 ms** |
| 5000-block outline: the save / its merge / what follows (instructions) | 729 M / — / 3113 M | **3.9 M / 62 M / 207 M** |
| 5000-block document: GUI blocked / longest stall | 357 / 124 ms | **28 / 26 ms** |
| 5000-block document: the save / its merge / what follows (instructions) | 795 M / — / 2942 M | **3.4 M / 79 M / 259 M** |
| 5000-block document: a letter's handling / key → frame p95 | 5.7 / 25 ms | **1.3 / 15 ms** |

What is left of an autosave on a 5000-block page (~20–28 ms, one stall): the merge — the index
patching its lists with the prepared note, `fileChanged` / `indexChanged` to the open editors —
and what follows it on the next turns. The small page's whole autosave is now below the probe's
12 ms threshold.

### Typing: a keystroke costs the block (2026-11-18)

A letter typed into a block of a 5000-block page still paid for the page after L4, outside the
document: `BlockListModel::apply()` rebuilt its uid → index table, compared the uid lists and ran
`refresh(true)` over every row (a hash lookup, the cached shape, the selection band, a parent
task's subtask count over its descendants, the rows with `((id))` rendered again); the changed row
was announced with every role; `PageController::markDirty()` made the text to save (a scan of
every block for a lone fence, `toString()`), compared it with the file's and parsed the
frontmatter for the note mode; the edited row's hidden rendered text was rendered again (the
`InlineRenderer`, the task chips) and laid out again by its hidden `Text` / tinted `TextEdit`; and
the inline editor detected its completion twice (the caret's signal comes before the text's).
Now:

* **The model's typing fast path** (`isTypingEdit`, `refreshTyping`): an edit whose changes are
  all `Changed` keeps the rows, their uids and indices; when it also kept the blocks' kind, depth
  and children, left the frontmatter alone and moved no line after them, only the edited rows, the
  rows that follow every change of the page (`m_dependents`: calc blocks, tables, charts, html
  with `((id))` or inline calculations, embeds of this page — each added where its flag is set,
  so a row rendered later is one too) and the ancestors of a block whose task status changed are
  refreshed; anything else is the full refresh as before. A changed row is announced with the
  roles of its text only (`TextRoles`), in both paths.
* **The dirty check of a typed block** (`PageController::TypingSave`): the first keystroke in a
  block compares the whole text as before and keeps what lies around the block's own text in the
  text to save — whether it is the file's text there and, if so, which own text would make the
  page clean. The next keystrokes compare the block's own text with that (dirty for certain, or
  the whole comparison when it matches: typed back means clean); a block saved with other text
  (a lone fence, the editor's space), another file text or any other edit compare in full. The
  note mode is looked at only when the first row changed.
* **The edited block's rendered text is kept** while it is edited (`BlockText.keptHtml`; its
  height then is the editor's), the latest taken when editing stops; the completion is detected
  once per keystroke (`InlineEditor`: the caret's change waits for the text's when the length
  differs).

`tst_editor_typing` types scripted and random edits (1500 steps per page: an outline, a page with
every kind of dependent, the same with CRLF) into two editors, one with the fast path off
(`setTypingFastPath(false)`), and compares every role of the rows a moving window looks at after
every step; every value that changed since the step before must have been announced with its role
(both paths), and the dirty flag must say whether the text differs from the file's. Removing the
dependents, the ancestors, the line check, the hooks in `renderHtml()` and `ensureTransclusions()`,
a role of `TextRoles` or the dirty check's comparison each fails it (mutation testing); the
children check and the calc consumers' flag are kept as safety (no difference found without them).
In that test (Debug, one process) a keystroke in a 5000-block outline costs 1.6 ms with the full
refresh and 0.3 ms with the fast path.

**In the app** (`bench-matrix.sh typing`, Release `-O3 -g`, Xvfb, base = `0f6bab95`, the commit
before; 8 runs per build in two alternating rounds while other work loaded the machine — load
about 22 on 16 cores; medians of the runs' medians, min–max in brackets):

| metric | outline5000 before | after | document5000 before | after |
|---|---:|---:|---:|---:|
| letter: handling | 2.8 ms [2.8–3.8] | 1.2 ms [1.1–2.2] | 3.3 ms [3.0–4.5] | 1.4 ms [1.2–1.8] |
| letter: handling, p95 | 4.2 ms | 3.5 ms | 6.4 ms | 3.0 ms |
| letter → frame | 11.1 ms | 10.8 ms | 13.1 ms | 10.8 ms |
| letter → frame, p95 | 14.7 ms [13.3–24.3] | 14.9 ms [12.7–54.7] | 19.5 ms [13.9–28.8] | 13.3 ms [12.6–30.5] |
| Enter: handling / → frame | 4.4 / 18.2 ms | 4.9 / 18.6 ms | 5.0 / 18.1 ms | 5.3 / 18.1 ms |
| undo: handling / → frame | 3.4 / 15.3 ms | 3.2 / 15.1 ms | 3.9 / 15.0 ms | 3.2 / 14.7 ms |
| GUI blocked after a burst (the autosave) | 165 ms | 174 ms | 152 ms | 165 ms |

A letter's own handling is less than half of what it was; on llvmpipe the frame (about 10 ms)
is now most of a keystroke, and the document's p95 meets the desktop budget. What is left of the
handling per key: `BlockDocument::version()` and `diffFrom()` for the undo step (a reference count
per block: about 0.1 ms at 5000 blocks, L4), the first keystroke of each burst (the whole
comparison, part of the p95), Qt Quick's own work for the inline editor. Enter, undo and the
autosave are unchanged (within the runs' spread: they take the full path). `typing-small`: the
same within the spread (a letter 0.9 → 0.7 ms).

### Index cache and search upkeep off the GUI thread (L18, 2026-11-18)

Two kinds of background work after edits still ran on the GUI thread, in slices: the index
cache (L2) and the full-text document of a changed note. The GUI thread now only takes a copy
and merges a result; the work runs on the index pool (`indexworkers.h`; WebAssembly, without
threads, keeps the slices).

**The index cache.** `IndexCacheKeeper` re-encoded the whole cache 60 s after every pause in
the index's changes — at 10 000 notes 158 ms of encoding (18.9 MB) in 8 ms slices on a 1 ms
timer, so the GUI thread was ~89 % busy for a third of a second after every pause in typing (on
a 120 Hz phone 0.5–1 s of dropped frames while the user reads or scrolls). Now:

* **When:** after a load that parsed (5 s, once), when the app asks — `saveIfDirty()`, called by
  `Workspace` when the application state leaves Active (Inactive, Hidden or Suspended: another
  window has the focus, the phone shows another app; the second such state finds nothing to do)
  and in `close()`
  — and, while the app stays in front, at the latest `Policy::maxDirtyMs` (10 min) after the
  first unsaved change; a pause in typing no longer starts or postpones anything.
* **Where:** `indexcache::SnapshotTaker` copies what a file holds on the GUI thread in steps of
  `Policy::sliceMs` (4 ms; given up when the index or its full-text index changes in between,
  tried again 2 s later) — the notes' records are shared (`std::shared_ptr<Entry>`: a change
  attaches a new record, so a snapshot's stay as they were) and the posting lists implicitly
  shared, so a step costs reference counts, not data. The `Snapshot` is immutable: `encode()` and
  `QSaveFile` run on a pool thread, which the workspace's pool waits for when the app ends
  (`QThreadPool`'s destructor). Snapshots are numbered: a write never replaces the file of a newer
  one (a periodic save still writing when the notebook closes). `saveIfDirty()` takes its snapshot
  at once (nobody is looking), first building up to 16 pending full-text documents.
* **The file** is unchanged (format 2): a snapshot taken while a few full-text documents were
  pending writes the documents it has (each with its note's content key; `adoptSearch()` on load
  keeps the current ones and builds the rest), and the keeper stays dirty until a complete one is
  written. Before, such a cache had no search section at all, so the next load built every
  document again.

Laptop (i5-13450HX, Release, unpinned, a shared machine in a quieter hour — 1-minute load
6–16 —, `bench.sh -r 5`, medians, `compare`: no row changed beyond max(10 %, noise); base =
`0f6bab95`, both trees with this branch's `tests/bench`):

| | 1 000 notes | 10 000 notes | 50 000 notes |
|---|---:|---:|---:|
| `cacheEncode` — base: the GUI thread's work per save, in 8 ms slices | 14.0 ms | 159 ms | 885 ms |
| `cacheEncode` — now on a pool thread (snapshot + encoding) | 14.3 ms | 159 ms | 847 ms |
| `cacheSnapshot` — the GUI thread's work per save now, in ≤ 4 ms steps | 0.26 ms | 5.2 ms | 49 ms |

At 10 000 notes a periodic save costs the GUI thread two steps of a few milliseconds instead of
some 20 slices of 8 ms, at most every 10 minutes instead of a minute after every pause. A first
version copied every note's `ParsedNote` (its dozen implicitly shared members: a cache miss
each): 23–33 ms at 10 000 notes; sharing the records cut that by ~5×. (The copy of the posting
lists — one per term — is the smaller part.)

**The full-text document of a changed note (L18).** 300 ms after the last change `searchTick`
tokenised the changed notes (`SearchIndex::prepare`) on the GUI thread, at least one note per
8 ms slice — a 1 MB note in one go. With a worker (`VaultIndex::setSearchWorker()`, given by
`IndexLoader::start()` from its pool) the index hands the dirty notes (at most 64 notes or 256 K
characters per job, one job at a time; removals are dropped right away) with shared copies of
their texts to the pool and applies each prepared document with `SearchIndex::set()` only while
the note still has the content key it was prepared from and was not marked again meanwhile
(`searchInFlight` holds the job's ticket per note; an edit meanwhile drops the document, the note
is prepared again). `flushSearch()` — `search()` calls it — stays synchronous: it builds the
notes still out on the worker too, and their late documents are dropped. Measured:

| one note re-indexed in a vault of 1000 | 50 KB | 200 KB | 1 MB |
|---|---:|---:|---:|
| `searchLargeNote` (parse + tokenise + apply; the GUI thread's work before, also now in `flushSearch()`) | 2.6 ms | 9.9 ms | 43 ms |
| `searchApplyLargeNote` (the GUI thread's work now: `set()` of the prepared document) | 0.09 ms | 0.19 ms | 0.41 ms |

`searchLargeNote` itself is unchanged: 42.7 against 43.2 ms for 1 MB, six interleaved runs of
each tree at load ~10 (a `bench.sh` round, base first, showed +7 % within its ±100 % noise).
(The parse of the note, `VaultIndex::update()` after a save, still runs on the GUI thread — L4/L12
made it once per save.) Diffing the postings of the changed terms only was not needed: replacing
a 1 MB note's document costs less than half a millisecond.

Tests: `tst_index_cache` (a snapshot given up on index and full-text changes, immutable while the
index changes on another thread, equal bytes in steps and at once, a partial search section
adopted), `tst_index_search` (stale documents dropped while a job is out, `flushSearch()` and the
index's end with a job out, scripted edits on a pool and inline equal to a synchronous build),
`tst_notebook_indexcache` (written on `saveIfDirty()` and not after pauses, after `maxDirtyMs`
while changes never pause, a keeper closed while writing, the files load to the edited index),
`tst_shell_workspace_wiring` (the app inactive writes the cache, the next open takes every note
from it).

### Z10 stage A: rendered text from runs (2026-11-18)

The first stage of Z10 (docs/UI.md "Rendered text without rich text: `RenderedTextItem`"): a
block's rendered text from `InlineRenderer::renderRuns()` (text + style runs + source map,
`markdown/renderedtext.h`; `runsToHtml()` gives `render()`'s HTML back byte for byte, checked for
all 764 renderings the renderer's tests make) drawn by one C++ item — a cached `QTextLayout`, one
`QSGTextNode`, the chips' shapes as one geometry node — instead of a rich-text `Text` / tinted
`TextEdit` + `RichTextPills` + `BTintShapes`. Behind `BRULION_TEXT_ITEM=1`, off by default.

Measured on the x86_64 desktop (shared: load 15–20 for the 5000-block rows, ≈ 1 for the 300-block resize), Release `-O3 -g`, Xvfb +
llvmpipe, the 10 000-note bench vault (`make-bench-vault.py`), `BRULION_TEXT_ITEM=0` vs `1` with the
same binary:

| | rich text | `RenderedTextItem` |
|---|---:|---:|
| GUI-thread instructions per row created (scroll probe, wheel, `--probe-cache 0`, 300-block page; passes 3 − 1 over 1158 rows, 3 runs, medians, spread ≤ 6 %) | 14.5 M | 13.9 M (−4 %) |
| `page-resize-medium` (300 blocks, created whole): request → frame, medians of 6 interleaved runs of 12 resizes (a quiet machine, load ≈ 1) | 248 ms (max 295) | 145 ms (max 188), −41 % |
| `page-resize-outline5000` (rows in the cache only) | 139.5 ms | 115.0 ms (−18 %) |
| `page-scroll-document5000`: frame interval p95 / frames > 33 ms | 19.6 ms / 0 | 19.6 ms / 2 |
| `open-outline5000` / `open-document5000`: navigation → frame | 185 / 203 ms | 172 / 224 ms |
| RSS after opening / scrolling the 5000-block pages | 784 / 802 MB | 782 / 803 MB |

What it says: the text of a row was a small part of the row. A row of the editor is still a
`BlockDelegate` with some 45 objects (bullet, task box, Loaders, handlers, the `BlockText` with its
hidden `Text`, overlay `Item` and handlers); taking the `QTextDocument` out removes about 0.6 M of
the row's 14.5 M instructions and no measurable memory. The width change is where the item helps
(only the lines a new width breaks are laid out again — what QQuickText does; synchronously, so
every row's height is right in the same frame), opening and scrolling are within the noise. (One
earlier run of `page-resize-medium` under load gave the rich text 54 ms after an 800 ms first
resize; the interleaved runs on the quiet machine did not repeat it.) Stage A
is therefore the foundation (runs with a source map, an item that draws them, parity checked) for
the stages that pay: A2 (a journal day as one item) and C (one item per page, only the visible rows
laid out, the per-row QML gone).

Differences left, all small: under X11/OpenGL the item's text shows subpixel colour fringes where
the rich `Text` is grey (both are `QSGTextNode`s with the default render type; not found yet); the
software renderer's hover underline is 1 px where the rich text's is 2 px; whitespace inside a page
differs by antialiasing only (≤ 0.7 % of the pixels, none by more than 48 of 255, editor
playground screenshots `page`, `tags`, `markup`, `linkparts`, `document`, `properties`, desktop and
phone).

### L10: popups and menus not made with the shell, `BIcon` without a `Loader` (2026-11-18)

**What the shell made that the first screen does not show.** The bench probe now counts QObjects
(QtCore's AddQObject / RemoveQObject hooks) and the GUI thread's instructions at every startup
mark, and `BENCH_OBJECT_TYPES=1` lists the live objects by class and by the named ancestors under
`AppShell` (tests/bench/app/README.md, `benchprobe.cpp`). On main (438d6eb7), the sample notebook
at 1280 × 800, first content: 13,316 live QObjects, of which

| what (closed or hidden on the first screen) | objects |
|---|---:|
| the shell's dialogs: new page 450 (its folder picker included), share 218, move 204, palette 196, extension question 188, switcher 187, rename 184, about 180, delete 173, quick capture 154 | 2,134 |
| menus: the top bar's More 841 (its Notebook submenu 282), AI 188 and tab switcher 170; the tree's row menu 462 and "+" menu 189; the tab bar's 188; the rail's notebook switcher 103; the breadcrumb's overflow 39 | 2,180 |
| the side panel (hidden) 176, the update banner (no update) 77, the journal's "Go to day" dialog (a month grid) | ≥ 253 |
| `BIcon`: 295 icons, each an Item, a `Loader`, the Loader's component and context, an `IconImage` (198: the others have no name) and its own recoloured texture | ~1,300 |
| a phone (412 × 915): the closed drawer's rail and page tree | 1,017 |

The kept views of "Switching views" are built 1.2 s after the notebook is ready, after the first
content: unchanged.

**What changed.** No popup is made with the shell. The four used most — the switcher (the top
bar's search field), the palette, the top bar's More menu and quick capture — are built ahead 3 s
after the notebook is ready, one every 0.4 s, each by an asynchronous `Loader` (incubated over
several frames, the way the kept views are; opened before it is ready, the rest is made at once).
Every other dialog and menu is made the first time it opens and kept (`AppShell.newPageNow()`,
`ShellTopBar.aiMenuNow()`, `ShellSidebar.contextMenuNow()` …; the extension question with the first
question), and so are the journal's "Go to day", the side panel (the first time it shows) and the
update banner (once a newer version is known). A phone's drawer content is built asynchronously
0.6 s after the notebook is ready, or made when the drawer first opens. `BIcon` is an Item with
one `Image` whose picture comes from an image provider (`src/app/ui/iconprovider.h`) that draws
exactly what `IconImage` drew (tests/app/icons compares every pixel at device pixel ratio 1 and 2);
Qt Quick's pixmap cache shares one image and one texture among the icons of a name, size and
colour.

**Measured** (`tests/bench/app/run-bench.py`, scenario `startup`, Release builds of main and of the
branch, Xvfb + llvmpipe (`LP_NUM_THREADS=1`) on the shared 16-core x86_64 desktop, load 15–35 from
other agents' builds; two interleaved rounds of 3 runs per build, medians [min–max]; the
instructions and object counts are the GUI thread's / the process's and barely move between runs,
the milliseconds are wall time on that loaded machine):

| | main | branch |
|---|---:|---:|
| demo, first start: GUI instructions to the first frame | 2,556 M | **1,688 M** (−34 %) |
| … to the first content | 3,221 M | **2,381 M** (−26 %) |
| … GUI CPU to the first content | 823 ms [439–1,179] | **491 ms** [412–559] |
| … QObjects alive at the first frame / first content | 10,899 / 13,313 | **2,379 / 4,721** (−65 % at content) |
| … first frame / first content's frame | 997 ms [468–1,768] / 1,445 ms [717–3,117] | **702** [651–793] / **887 ms** [789–989] |
| … RSS when ready | 344.4 MB | **331.3 MB** |
| demo, warm start: GUI instructions to the first content | 3,172 M | **2,276 M** (−28 %) |
| … first content's frame | 969 ms [561–2,552] | **696 ms** [537–1,044] |
| demo, phone (412 × 915, `--touch`): GUI instructions to the first frame / content | 2,534 / 3,099 M | **1,505 / 2,237 M** (−41 / −28 %) |
| … QObjects alive at the first frame / first content | 10,923 / 13,042 | **1,745 / 3,828** (−71 % at content) |
| … first content's frame | 1,616 ms [989–2,735] | **871 ms** [736–1,015] |
| 10k notebook, first start: GUI instructions to the first frame / content | 3,251 / 4,069 M | **2,388 / 3,215 M** (−27 / −21 %) |
| … QObjects alive at the first content | 15,241 | **6,481** (−57 %) |
| … first content's frame / ready | 1,364 ms / 3.19 s | **1,118 ms / 3.18 s** |
| … RSS when ready | 583.3 MB | **563.2 MB** |
| 10k notebook, warm start: GUI instructions to the first content | 4,009 M | **3,100 M** (−23 %) |
| … first content's frame (GUI CPU 783 / 917 ms: the machine, not the work) | 938 ms [791–1,134] | 1,276 ms [835–1,601] |

A later round when the machine had quietened (load 4–5; 4 runs per build, main's then the
branch's), the wall times main → branch: the first content's frame on the demo's first start
857 → **675 ms** [613–1,008], warm 701 → **462 ms** [435–538], the phone layout 796 → **568 ms**
[540–574], the 10k notebook's first start 1,309 → **689 ms** [688–712] and warm 905 → **579 ms**
[539–611]; the GUI thread's CPU to the first content 532 → **371 ms** (demo) and 869 → **456 ms**
(10k); the stalls after "ready" stay as they were (the longest 66–75 ms on the phone layout
against 65–92 ms).

The shell itself (`window` → first frame) now takes ~880 M instructions instead of ~1,750 M and
~2,000 objects instead of ~10,500. "settled" is not compared: on the branch it often comes before
the kept views are built (no stall over 24 ms in between), on main after them.

The cost moved, not all of it gone: a popup made when it first opens pays for its objects then.
The quick switcher opened 1 s after the 10k notebook is ready (`switcher` scenario, 3 runs each),
before the build-ahead, took 150 M GUI instructions and ~1,300 objects instead of 50–70 M and
~1,100 (its rows) — 218–367 ms instead of 201–229 ms on the loaded machine, yet 216 ms [201–225]
instead of 317 ms [274–339] in the quiet round (the rest of the startup's work is lighter); opened
after the build-ahead, or the second time, it costs what it did. The other dialogs and menus are opened
rarely; the largest, the new-page dialog, is ~450 objects.

Not measured on the Raspberry Pi or a phone (the plan's estimate was the Pi demo's first content
−0.3 s; L10b below).

### Budgets

Desktop = the laptop above (the graph: a GitHub runner, see its row), phone = Pi 4 (floor) and the
emulator (optimistic), web = headless Chromium (×4 CPU = phone). ✅ meets, ⚠️ within 1.5×, ❌ misses
(current code).

| budget | target | desktop | phone | web |
|---|---|---|---|---|
| start → first content | desktop < 1 s, phone < 2 s | ✅ 0.40 s (10k), 0.39 s demo | ✅ Pi demo 1.25 s, 10k 1.57 s | ✅ repeat 0.61 s, ×4 CPU 1.64 s; ❌ first visit at 20 Mbit/s 7.3–7.4 s (L9; 8.4–8.5 s before in the same session) |
| index ready | desktop < 2 s at 10k; phone < 10 s at 10k | ✅ 0.96 s, 0.83 s from the index cache (wave 5; was 1.89 s) | ❌ Pi 15 s before wave 5, not measured since (L1b) | ✅ 1k cold 1.3 s, warm 1.1 s (×4 CPU 5.8 / 4.7 s; was 1.1 / 5.2 s in the same session) |
| GUI stall while indexing / after ready | < 50 ms desktop, < 100 ms phone | ✅ 28 ms indexing, 0 ms after ready (wave 5) | ❌ Pi 0.54 / 0.45 s before wave 5 (L1b; the full-text part of it off the GUI thread since L18) | – |
| keystroke → frame (≤ 5000 blocks) | p95 < 16 ms desktop, < 33 ms phone | ✅ small page 11 ms; ✅ 5000-block outline p95 15 ms, document 13 ms ("Typing: a keystroke costs the block"; was 15 / 20 ms in the same session) | ✅ small page; ⚠️ outline p95 42 ms | – |
| Enter / undo | < 50 ms desktop, < 100 ms phone | ✅ 15 / 13 ms (L5; base in the same session 15 / 32 ms) | ✅ Enter 38 ms; undo 214 ms before L5, not measured since (L5b) | – |
| autosave stall (the longest stall after a save; the Zed-like aim: < 8 ms of GUI time) | < 50 ms desktop, < 100 ms phone | ✅ small page: none over 12 ms (the save 0.4 ms on the GUI thread; 131 ms on main with the views built ahead, 41 ms before them); ✅ 5000 blocks 22–26 ms (113–124 ms on main; what is left: the merge into the index) | ✅ small page 29 ms; ❌ 500 KB page 0.2 s | – |
| open page (5000 blocks) | frame < 150 ms desktop, < 300 ms phone | ✅ 113 ms | ❌ Pi 653 ms (L7) | – |
| Tasks / Calendar / Agenda | < 200 ms desktop, < 500 ms phone | ✅ 133–166 ms | ❌ Pi 0.73–1.28 s before L3, not measured since (L3b, L10) | – |
| Graph (4000 nodes; 1200 phones) | settled < 1.5 s desktop, < 3 s phone; GUI stall during the layout < 16 ms desktop, < 33 ms phone | ❌ not the laptop (unreachable): a GitHub runner with llvmpipe, CPU-rendered frames — stall 219 ms (was 958 ms), settled 2.0 s (`views`; was 8.3 s), 9.2 s with drags (was 40 s), first frame 0.13 s (was 1.07 s); the GUI thread's own sync 0.1–6 ms | ❌ Pi 2.6 s before L16, emulator 1.5 s; not measured since (L16b) | – |
| after a save, all derived lists (10k) | < 30 ms desktop, < 150 ms phone | ✅ 6.0 ms (L3; base in the same session 112 ms) | ❌ Pi 0.95 s before L3, not measured since (L3b) | – |
| idle | < 0.5 % CPU | ✅ 0.1 % | ✅ background 0 % CPU, 0 wakeups/s | – |
| memory | ≤ 100 MB + 10 B per Markdown byte (10k ≤ 330 MB) | ⚠️ 340 MB heap (wave 5; was 512 MB; index alone 221 MiB) | ❌ emulator PSS 359 MB at 5k before wave 5 (< 300 MB wanted; not measured since) | ✅ 86 MB of linear memory at 1k (was 104 MB) |
| download | APK < 40 MB, web < 10 MB | – | ✅ 34.1 MB download (92.7 MB APK; L20: was 36.1 / 105.9 MB; L9 on 2026-10-30: 26.3 / 80.6 MB) | ❌ 17.1 MB gzip (11.3 MB brotli, not served; L20: was 18.5 / 12.1 MB; L9 on 2026-10-30: 13.5 MB) |

### Next steps

The L-items of the October 2026 performance plan, with the numbers they should move:

| # | change | expected |
|---|---|---|
| L1b | cheaper merge of prepared notes on the GUI thread: link lookup keys (`cleanTarget`, NFC, lower case) and the name table's keys computed by the loader's workers; fewer redraws while loading (the browser's cold load: +11–16 % to "ready", 30 %–2.5× more draws since journals come first); measure the Pi and the Android emulator (L1/L2 of wave 5 not measured there yet) | laptop 10k index ready 0.96 / 0.83 s → 0.6 s; Pi ≤ 6 s cold, ≤ 3 s warm |
| L5b | measure undo / redo on the Pi (`bench-matrix.sh` typing on the 5000-block outline, Release) and record the `blockDocOps` 5000 blocks undo+redo baseline in `pi.csv` (`perf-check.sh -u` in a quiet window; until then the Pi's check skips the row) | Pi undo 214 ms → < 50 ms |
| L3b | measure the after-a-save fan-out on the Pi (`bench_reminders` afterEdit, small profile) and ratchet its `pi.csv` row (`perf-check.sh -m pi -u` in a quiet window; the row now measures the cached path, so the old 963 ms baseline only passes it loosely) | Pi 0.95 s → < 0.2 s |
| L7 | cheaper page opening (header summary and linked references from the index) | Pi open 653 ms → ~400 ms |
| L9b | the developer and self-test QML (`qml/dev`, `qml/content/*SelfTest.qml`) in a folder outside `src/app` (qmlimportscanner scans the target's source folder whole, and its `-exclude` does not work there), so that releases leave out QtTest, QuickTest and `Qt.labs.folderlistmodel` | APK −0.84 MB, wasm −0.27 MB (before wasm-opt) |
| L10b | measure L10 (popups and menus not made with the shell, `BIcon` without a `Loader`; done on 2026-11-18, above) on the Raspberry Pi and a phone: `bench-matrix.sh startup-demo`, the phone layout too, and the first opening of the More menu and the switcher before and after their build-ahead | Pi demo first content −0.3 s |
| L16b | measure the graph on the Raspberry Pi (`bench-matrix.sh views graph`, offscreen: the software renderer's image node, 3 000-note vault; must not regress) and in the Android emulator probe (`android-batch.sh probe`, the scene graph on GLES); check it by hand on a phone, on Windows with a GPU (Direct3D 11) and in a phone's browser; if `rebuild()` (VaultIndex::graph and the filters, GUI thread) costs a phone > 50 ms, move its pure part onto the worker | phone: GUI stall during the layout < 33 ms, settled < 3 s |
| L19 | measure the PDF viewer on real devices (the emulator: 174 MB PSS at 800 %, above): PSS with a 100+ MB scanned lecture zoomed in on a phone (if above 350 MB, base pictures at half resolution on touch devices), time to the first page of a 300-page lecture, Qt PDF timings in a Release build | phone PSS < 350 MB; first page < 0.5 s |
| L20b | measure the generated QML at `-Oz` on a phone (L20): a Release APK built with `-DBRULION_QML_CODE_FOR_SIZE=OFF` against the default one, `android-batch.sh probe` or the Pixel — journal scroll, opening a 5000-block page, the views; if a view loses more than 10 %, compile its few hot files at the build type's level | no phone slowdown > 10 % |

The Z-items of "Only what is on screen, only what changed" (above) come first (Z1 done on
2026-11-18: the save on a writer thread): Z1–Z9 remove most
of an autosave's GUI time in a big notebook, Z10 is the editor's rewrite in stages.

Done in wave 5 (2026-10-29, above): L1 (parse on worker threads), L2 (the index cache), L8 (the
memory diet); on 2026-10-30 L5 (delta undo/redo, above; the Pi still to be measured: L5b), L9 (native file
pickers, one Qt Quick Controls style: APK −15.6 MB, wasm −2.3 MB gzip, above), L3
(incremental derived lists, above; the Pi: L3b) and L16 (the graph's layout on a worker thread,
drawn through the scene graph, above; the phones: L16b). Also
open: serving brotli for the web build (9.4 instead of 13.7 MB; GitHub Pages serves gzip only) and
the ~90 ms later content on a desktop first start (above). On 2026-11-07 L20 (the generated QML code for size, Android's
library without exported static-library symbols and with RELR, no QML source in Android and
browser releases: APK −13.2 MB, wasm −6.0 MB raw / −1.4 MB gzip, above; the phone: L20b). On
2026-11-15 L4 (typing in place: the keystroke's core 1.3 → 0.12 ms, above) and L12 (a sync that
reads no settled note and no second copy of its state: 0.74 → 0.30 s at 10k, above; the Pi is
still to be measured). On 2026-11-18 L18 (a big note's full-text document tokenised on a worker,
the GUI thread applies it in 0.2–0.4 ms) with the index cache written from a snapshot on a
worker (above: "Index cache and search upkeep off the GUI thread").

### How to run the benchmarks

Never time anything on a machine that is building or running another benchmark; use Release
builds; take ≥ 5 repetitions and compare medians with their spread (`(max − min) / median`).

**Core (QtTest `QBENCHMARK`, `tests/bench/`, driven by `scripts/bench.sh`).** One executable per
core module (`bench_text`, `bench_dates`, `bench_calc`, `bench_markdown`, `bench_index`,
`bench_vault`, `bench_sync`, `bench_reminders`, `bench_io`), built only with
`-DBRULION_BUILD_BENCHMARKS=ON` (`bench.sh` configures a Release build dir for you):

```sh
nix develop
JOBS=16 scripts/bench.sh -B -b ../build-bench                  # configure Release + build the suites
scripts/bench.sh -n -b ../build-bench -r 5 -c 0-11 -q 3 -o ../r/new bench_index bench_markdown
scripts/bench.sh -n -b ../build-bench bench_index:search       # one function
scripts/bench.sh -n -b ../build-bench "bench_index:search:10000 notes fuzzy"   # one data row
scripts/bench.sh -n -b ../build-bench -r 3 -T 600 bench_index    # at most 10 min (CI: -T 1500)
scripts/bench.sh compare ../r/base ../r/new                    # % change against the noise
scripts/bench.sh scaling ../r/new                              # exponent k between input sizes
scripts/bench.sh -h
```

`-c` pins to CPUs (on a hybrid Intel CPU use the P-cores: `0-11` on the i5-13450HX), `-q` waits
for a quiet machine (1-minute load below the value), `-p small` drops the largest sizes on
Pi/phone-class machines (3.7 GB RAM). `bench.sh` holds the machine-wide build lock for the whole
run. Results: `<dir>/raw/*.csv|txt` (QTest output per repetition), `<dir>/summary.csv` (median,
min, max, spread per row), `<dir>/env.txt` (machine, governor, load, git state, bench source
hash).

* **Stale binaries are refused.** Every suite carries a hash of the `tests/bench` sources it was
  built from (`tests/bench/sourcehash.cmake`, recomputed on every build); `bench.sh` compares it
  with the source tree and stops with exit code 2 on a mismatch. The same step makes Ninja
  rebuild a suite whose source content changed even when a copied tree kept older mtimes than
  the build (the trap behind the wrong "23 ms per keystroke on the Pi").
* **Inputs are identical on every machine.** The generators (`tests/bench/benchdata.h` and the
  suites) draw from the RNG at most once per statement — C++ leaves the order of `a + b`
  operands and of call arguments unspecified, and GCC evaluated them differently on x86_64 and
  aarch64, so the Pi used to measure a 0.7 % different vault. Each generated input prints
  `[bench] input <name>: <bytes> bytes, sha256 <digest>` (and `[bench] vault N notes: … sha256 …`)
  into `raw/*.txt`: compare two machines' lines to prove they measured the same thing.
* To compare two source trees, give both the same `tests/bench` sources (the hash in `env.txt`
  must match), build each in its own directory and run the suites alternately under the same
  conditions (same `-c`/`-q`), then `compare` the two result dirs.

* **Index loading** (wave 5): the app and brulion-cli read the index cache from the device state
  folder; `BRULION_NO_INDEX_CACHE=1` neither reads nor writes it (a cold index on every start),
  `BRULION_INDEX_THREADS=<n>` sets the parse workers (`0`: in slices on the GUI thread, the
  browser's way). The core rows are `bench_index:openFromDisk` "loader …" and "loader warm cache
  …" (the cache written in the row's setup), `cacheEncode`, `cacheDecode`, `prepare`.
* **Before/after on GitHub's runners** (x86_64 and arm64, the flake's toolchain, both trees with
  head's bench sources, two alternating rounds): `gh workflow run perf-compare.yml --ref <branch>
  -f base=<ref> [-f suites="bench_index bench_markdown"] [-f reps=5]` — the comparison lands in
  the job summary and an artifact. Shared runners are noisy: relative changes only. `-f app="views
  graph"` (`-f suites=` for only these) runs app benchmark groups too: both trees' `brulion` in
  Release with Qt from aqt, the probe and the analysis from head, Xvfb + llvmpipe on
  ubuntu-24.04, `app_rounds` × `app_runs` runs per build, alternating (report.py's table in the
  summary). A workflow can call it too (`workflow_call`), e.g. a branch's own push trigger before
  the file reaches the default branch, which `gh workflow run` needs.

**App (the real binary, `tests/bench/app/`).** An `LD_PRELOAD` probe records frames, GUI stalls,
memory and CPU and drives scenarios (startup, open-page, typing, search, switcher, views, graph,
journal-scroll, switch-pages, idle, workload) with real input; no hook in the product code, so
any build (v0.1.1 included) can be measured.

```sh
tests/bench/app/make-bench-vault.py /tmp/vaults/big10k           # deterministic 10k-note vault
brulion --demo --vault /tmp/vaults/demo --screenshot /tmp/x.png  # the sample notebook, once
nix develop -c tests/bench/app/build-probe.sh /tmp/probe
nix develop -c nix shell nixpkgs#xvfb -c env BINS="base=…/brulion cur=build-rel/src/app/brulion" \
  PROBE=/tmp/probe/libbenchprobe.so VAULT_DEMO=/tmp/vaults/demo VAULT_BIG=/tmp/vaults/big10k \
  OUT=/tmp/results RUNS=5 tests/bench/app/bench-matrix.sh startup-big typing views
tests/bench/app/analyze.py /tmp/results --csv /tmp/results.csv
```

On the Pi use `PLATFORM=offscreen` (software renderer), run inside `nix develop` (QML import
paths) and under the build lock, with a 3k-note vault (`make-bench-vault.py … --notes 3000`).
Attribution: `tests/bench/app/profile-cases.sh` (perf with probe windows, qmlprofiler,
heaptrack). Details: [tests/bench/app/README.md](../tests/bench/app/README.md).

**An autosave's cost on the GUI thread** (the Zed-like budget: < 8 ms per save). `autosave_blocked`
only sums stalls over 12 ms; in the typing windows of `typing` and `source-typing` (each burst
and the 2.4 s after it) the probe also records signal spans through Qt's signal spy callbacks
(QtCore's private `QSignalSpyCallbackSet`, GUI thread only, with the QML part of each emission
through the wrapped `QAbstractDeclarativeData::signalEmitted`) — inclusive and self wall time,
thread CPU time and user-space instructions (`perf_event_open` with `exclude_kernel`, allowed by
`perf_event_paranoid` 2; both PMUs of a hybrid Intel CPU) —, the GUI thread's `fsync` / `rename`
calls (interposed) and its CPU at every lag tick. `analyze.py` turns them into `save_sync_ms`
(the `timer:save` span: PageController's whole synchronous save), `save_write_ms` (up to
`Vault::changed`), `save_fsync_ms`, `save_vault_changed_ms`, `save_cpu_ms`, `save_insns_M`,
`sig_<signal>_ms`, `fanout_deferred_cpu_ms` / `fanout_insns_M` (the GUI thread's CPU from the
save's end over 1.7 s minus its idle rate: the deferred work, pieces under 12 ms included),
`fanout_model_resets`, `fanout_rows_changed` and the top spans by self time; `kept_views` says
how many of the views ShellRouteView builds ahead existed when the burst began, and
`BENCH_WAIT_KEPT=1` waits for all of them first. Instructions are the number to compare on a
shared machine (wall times there include the other builds). What the callbacks see and miss
(queued connections, QML handlers): the README's "Autosave: where the GUI thread's time goes".
The core row beside it is `bench_index:saveRoundTrip` (a save's write, `Vault::changed`, index
update and `changesSince()` without the GUI, 1000 / 3000 / 10 000 notes on disk).

```sh
nix develop -c tests/bench/app/build-probe.sh /tmp/probe
nix develop -c nix shell nixpkgs#xvfb -c env -u WAYLAND_DISPLAY -u DISPLAY BINS="cur=…/brulion" \
  PROBE=/tmp/probe/libbenchprobe.so VAULT_DEMO=/tmp/vaults/demo VAULT_BIG=/tmp/vaults/big10k \
  OUT=/tmp/save RUNS=3 tests/bench/app/bench-matrix.sh typing
tests/bench/app/analyze.py /tmp/save | grep -E 'save_|fanout_|sig_|top|kept'
tests/bench/app/timeline.py /tmp/save/typing-outline5000-cur-00.jsonl --span-ms 0.5   # one run's spans
```

**Android and browser (`tests/bench/mobile/`).** APK size reports (`android-apk.sh`,
`size-report.py`), an emulator probe build (a debuggable measurement copy, never shipped),
launcher starts and `dumpsys meminfo` (`android-batch.sh release|probe`), idle and alarms; the
browser through `wasm-bench.mjs` (headless Chromium over CDP: first and repeat visits, an
IndexedDB vault, CPU and network throttling, IDBFS write-back, `index_ready_ms`) and
`wasm-batch.sh`. Details and the full recipe:
[tests/bench/mobile/README.md](../tests/bench/mobile/README.md).

### Regression guards: `scripts/perf-check.sh`

`scripts/perf-check.sh` runs a fixed subset of the core suites through `bench.sh` and fails
(exit 1) when

* a **per-machine guard** row's median exceeds its baseline by more than
  max(10 %, 3 × spread), spread being the larger of the baseline's and this run's — baselines
  are committed per machine class in `tests/bench/baselines/<machine>.csv` (`laptop`, `pi`; the
  file also records the CPU, the bench source hash and the `bench.sh` options it was measured
  with, which the check reuses);
* a **scaling limit** is passed: the exponent k of time ~ n^k between the two largest sizes of
  a series (`tests/bench/baselines/limits.csv`) — a quadratic regression shows k ≈ 2 on any
  machine, while linear code stays at k ≈ 1;
* an **absolute limit** is passed (index heap with 10 000 notes, MiB), or a checked row is
  missing.

```sh
JOBS=16 scripts/perf-check.sh                  # build, run (~5 min on the laptop), check
scripts/perf-check.sh -n -s                    # scaling + memory only (machine-independent; CI)
scripts/perf-check.sh -n -u                    # new baseline after an optimisation (ratchet)
scripts/perf-check.sh -m pi -R <results dir>   # check an existing run
```

Ratchet a baseline down when an optimisation lands (`-u`); never loosen it without a note in
the commit message. A machine without a baseline gets only the limits. The guard rows (keep in
sync with the arrays in the script):

| guard | row | why |
|---|---|---|
| open vault | `bench_index:openFromDisk` 10000 notes scan+read+index | start with a big vault |
| first search | `bench_index:firstSearch` 10000 notes | full-text index build (N1) |
| query | `bench_index:query` 10000 notes tag praca | query blocks |
| suggestions | `bench_index:suggestBlocks` 'kawa ogród', `suggestPages` 'stro' (10000 notes) | `((` and `[[` popups, quick switcher (N7) |
| index memory | `bench_index:memory` 10000 notes + search index | ≈ 10 B of heap per Markdown byte since wave 5 (L8) |
| after a save | `bench_reminders:afterEdit` mixed vault 10000 notes, all consumers | every save's fan-out as the app runs it: the index's patched lists and the consumers' caches (L3; the free functions' "(stateless)" rows are beside it in the suite) |
| keystroke | `bench_markdown:blockDocOps` 5000 blocks keystroke | typing on long pages (L4) |
| undo / redo | `bench_markdown:blockDocOps` 5000 blocks undo+redo | an undo step applied as a delta (L5) |
| UI state store | `bench_vault:localStoreFlush` 10000 synced files | N9 |
| sync | `bench_sync:syncNoChanges` 10000 files (Pi: 5000) | L12 |
| open vault, threads | `bench_index:openFromDisk` 10000 notes loader 4 workers (Pi: 3) | L1 (wave 5) |
| open vault, cache | `bench_index:openFromDisk` 10000 notes loader warm cache 4 workers (Pi: 3) | L2 (wave 5) |
| a save | `bench_index:saveRoundTrip` 10000 notes throttled | an autosave's write, `Vault::changed`, index update and `changesSince()` (no baseline row yet: only its scaling limit counts until `-u` records one) |

Scaling limits (`limits.csv`): `foldKey` / `foldKeyWithMap` polish k ≤ 1.2 (N1),
`searchLargeNote` k ≤ 1.3 (N1), `parsePathological` inline HTML and one paragraph of `<`
k ≤ 1.3 (N6), `unlinkedLongLine` k ≤ 1.3 (N6), `htmlToMarkdown` flat and nested lists k ≤ 1.3
(N10), and from wave 4 `planRelocation` folder rename and page rename, `tagPageOpen` after a save,
`selectAllCopy` Markdown and plain text, `clipboardHtml` k ≤ 1.3 (measured k 0.98–1.10), from wave
5 `openFromDisk` loader 4 workers and loader warm cache 4 workers, `cacheEncode`, `cacheDecode`, from
L5 `blockDocOps` undo+redo (measured k 1.15) k ≤ 1.3 — 1.5 for all of them in the Pi's small
profile (3 workers); `saveRoundTrip` throttled k ≤ 0.25 between 3000 and 10 000 notes in both
profiles (a save must not grow with the notebook); index heap at 10 000 notes
≤ 233 MiB (wave 5: 220.6 MiB measured; it was ≤ 560 MiB before). The Pi baseline has no loader
rows and no undo+redo row yet (record them with `-u` in a quiet window; L1b, L5b).

App-level guards (startup, typing, open-page, views, idle) are not automated: run
`bench-matrix.sh` before a release and compare with the tables above (same rule: medians of 5
runs, max(10 %, 3 × spread)).

**Baselines** (`tests/bench/baselines/laptop.csv`, `pi.csv`) were recorded on 2026-10-18 from a
perf-check run of the current code and checked against an independent second run of the same
code (laptop: 19 of 19 checks pass, every row within ±5 %; Pi: 18 of 18). The same check on
d1141c0 fails as it should (Pi: first search +111 %, `((` suggestions +101 %, LocalStore flush
+119 % and all six scaling limits, k 1.70–4.04), and so does a scratch copy of the current code
with the old quadratic `foldKey` (`src/core/text/text.cpp` of d1141c0; laptop: first search
+91 %, `foldKey` k 2.28, `searchLargeNote` k 2.30 — exactly the three checks it touches).

### CI performance smoke

The `perf` job of `.github/workflows/ci.yml` builds `bench_text`, `bench_index`,
`bench_markdown` and `bench_io` (Release, Qt from aqt) and runs `scripts/perf-check.sh -n -s`
(3 repetitions): only the scaling limits and the index heap, never absolute times, so the
speed of a shared runner does not matter. It is **non-blocking** (`continue-on-error`): a failure
becomes a warning annotation and a job summary, the results are uploaded as an artifact.

It must not cancel anything either: a job that reaches its `timeout-minutes` ends "cancelled",
`continue-on-error` does not cover that, and the run ends cancelled too — through
`workflow_call` that skipped the whole v0.6.0 release once (the smoke takes 17–25 min, 30 with
the setup, and runners differ by up to 1.45×: an AMD EPYC 7763 against a 9V45). So the
benchmarks run with a time budget (`bench.sh -T 1500`: no repetition starts after 25 min, a
running one finishes; what is left is listed and its rows are `MISSING`, a warning), the steps
have timeouts of their own (Qt 15, build 20, the check 35 min) and the job's limit (80 min) is
above their sum. `release.yml` calls `ci.yml` with `skip-perf-smoke: true`: a release never
waits for the smoke, which runs in CI by hand and with `[ci]`.

Why it can run on shared runners without flaking: on the laptop's 4 E-cores, 16 runs of
`perf-check.sh -s` while other builds ran on the machine — 8 of them with a busy loop competing on
every one of those cores — gave 0 false failures; the largest exponent was k 1.13 against limits
of 1.2–1.3, while 8 of 8 runs of the quadratic `foldKey` copy failed (k 2.24–2.34). The index
heap is deterministic (544.8–544.9 MiB in every run). Absolute timings stay out of CI: runner
speed varies far more than the 10 % a regression check needs. The Pi (small profile, 2–3× size
steps, 1 MB L2 cache) shows k up to 1.27 for linear code, hence its limit of 1.5.

### Pitfalls (learned the hard way)

* **Cold vs warm index** (wave 5): a second run of the same notebook reads the index cache. App
  runs with `--start warm`/`cold` reuse the profile of their warm-up run and so its cache (written
  ~5 s after the index, or at the latest when the app closes); `--start first` is the cold index.
  Compare like with like, and set `BRULION_NO_INDEX_CACHE=1` for a cold index on warm starts.

* **Stale benchmark binaries.** `rsync -a` keeps source mtimes; a changed file that is older than
  the build used not to be recompiled (the "23 ms per keystroke on the Pi" was such a binary).
  Since the source hash it is rebuilt, and `bench.sh` refuses a binary built from other sources.
* **Unsequenced RNG calls** made the x86_64 and aarch64 vaults differ (22 705 922 vs
  22 869 397 bytes for 10 000 notes). Now both machines generate the same 22 869 397 bytes
  (SHA-256 `f638a871803a3058…`), and 24 of 24 inputs compared between the laptop and the Pi are
  byte-identical.
* **Process history matters**: a row measured after other functions of the same suite can differ
  from the same row in a fresh process (`suggestPages 'stro'`: 2.84 vs 3.31 ms). Compare like with
  like: whole-suite runs with whole-suite runs, perf-check runs with perf-check baselines.
* **Xvfb + llvmpipe has no vsync**: an animation (the splash's busy indicator) renders at
  ~800 frames per second and the GUI thread syncs every frame. Frame costs on the laptop are
  pessimistic; check surprising start-up numbers with `QSG_RENDER_LOOP=basic` or on the Pi.
* Pi / emulator / headless-browser numbers are not phone numbers: the Pi paints on the GUI thread
  (software renderer), the emulator's cores are 2–4× faster than phone cores, Chromium's ×4 CPU
  throttling slows the main thread only.
* I/O and allocation-heavy rows (`writeText`, `historySnapshot`, `query` on the Pi, LocalStore on
  the Pi) vary by ±20–100 %: read them as orders of magnitude, and expect wide tolerances there.
* **The emulator's `emu kill` is a power cut**: a vault pushed shortly before came back as 5 000
  empty files after the next boot, and two measurement windows ran on an empty vault without an
  error (PSS 178 MB instead of ~400 MB was the only hint). `emu.sh stop` now syncs the guest
  first and `push-vault` checks for empty files; check the vault after a reboot before trusting
  a big-vault number.
* Profiles (perf DWARF, qmlprofiler) slow the app 1.5–3×: use them for attribution, not timing.
