# App benchmarks (desktop and ARM)

Measurements of the real `brulion` binary from the outside — no hook in the product code, so the
same scripts measure any build (v0.1.1 and the working tree alike). An `LD_PRELOAD` library
(`probe/benchprobe.cpp`) rides along in the app's process: it records every frame (render thread
`beforeSynchronizing` → `frameSwapped`), GUI-thread stalls (a 4 ms precise timer; a gap > 12 ms
is a stall — the time input would have waited), memory (`/proc/self/status`, malloc in use) and
CPU, and drives the app through a scenario: real input through `QWindowSystemInterface` (keys
with shortcuts, wheel), navigation through the `AppController` singleton, and the editor through
the `pageEditor` / `pageController` objects. In the typing windows (each burst of typing and the
2.4 s after it) it also records signal spans, the GUI thread's CPU time and instructions, and its
`fsync` / `rename` calls — what an autosave costs and where that time goes ("Autosave: where the
GUI thread's time goes" below).

| File | What |
|---|---|
| `probe/benchprobe.cpp` | the probe (scenarios: `startup`, `open-page`, `typing`, `source-typing`, `select-copy`, `search`, `switcher`, `views`, `graph`, `journal-scroll`, `page-scroll`, `page-resize`, `switch-pages`, `idle`, `idle-edit`, `workload`, `pictures`, `memory`; see the header) |
| `build-probe.sh <dir>` | builds `libbenchprobe.so` against the dev shell's Qt (a throw-away CMake project in `<dir>`; Qt's CorePrivate and GuiPrivate headers) |
| `make-bench-vault.py <dir>` | a deterministic synthetic notebook: 10 000 notes (≈ 650 journal days over two years + pages in 40 folders, links, tags, tasks with dates and reminders, a few queries), `Bench/Konspekt 5000.md` (5000-block outline), `Bench/Dokument 5000.md` (5000-block document), `Bench/Mała strona.md`; `<dir>.pages.txt` lists 50 pages for page switching |
| `make-picture-vault.py <dir>` | a notebook of photos (ImageMagick: `nix shell nixpkgs#imagemagick -c …`): 60 JPEG photos of 12 MP (every fifth upright, every seventh turned by its EXIF orientation) and 10 PNG screenshots in `assets/`, `Album.md` (40 photos on one page), 30 pages of four photos each, 30 pages of text and 30 journal days (twenty with two photos); `<dir>.pictures.txt`, `<dir>.pages.txt` and `<dir>.album.txt` list them for the `pictures` scenario |
| `run-bench.py` | runs one scenario N times (fresh working copy of the vault per run; `--start first\|warm\|cold`; `--platform xvfb` — Mesa's llvmpipe, every texture in the process —, `offscreen`, or `weston`: a headless Weston with its GL renderer and the app on the machine's GPU, `nix shell nixpkgs#weston`, `WESTON_GPU=pci-0000_00_02_0` where there are several); writes one JSONL per run |
| `bench-matrix.sh` | the whole matrix for several labelled builds (`BINS="base=… cur=…"`; `ENVS="cur=VAR=value"`: one label's environment, e.g. the same binary with `BRULION_PAGE_ITEM=1`), group by group (`startup-big-short`: first + warm starts only, for slow machines; `page-scroll`, `page-resize`: the 300-block and 5000-block pages) |
| `analyze.py <dir>…` | metrics per run → median / min / max / p25 / p75 per metric and label (`--csv`, `--json`) |
| `timeline.py run.jsonl` | one run's events in time order (marks, input, stalls with their CPU, model signals, signal spans ≥ `--span-ms`, the GUI thread's file system calls, frame gaps) |
| `perf-scenario.sh` + `perf-slice.py` | a scenario under `perf record` (CLOCK_MONOTONIC; user space only, `EVENT` default `cycles:u` with `--user-callchains` — what `perf_event_paranoid=2` allows —, `FREQ` or `PERIOD`) and the GUI thread's samples sliced by the probe's windows (`after_ready`, `stalls:100@content:ready`, `autosave`, `save`, `merge` — a background save's result merged —, `fanout`, `keys`, …) |
| `perf-attach.sh` | `perf record -p` attached only once the probe writes a given mark, for N seconds (slow machines: the Pi), then `perf script` + per-thread/DSO summary |
| `profile-cases.sh` | named attribution runs: perf cases (`typing-outline`, `source-typing-outline`, `startup-big`, `views`, `switcher`, …: `perf-scenario.sh` + `perf-slice.py` with the right windows), qmlprofiler cases (`qml-*`) and heaptrack (`heap-startup-big`) |
| `qtd-summary.py` | a qmlprofiler trace summarised (build with `-DCMAKE_CXX_FLAGS=-DQT_QML_DEBUG`) |
| `laptop-build.sh` | Release(+`-g`) builds of a source tree on the x86_64 laptop (`rel`, `qmldbg`) |

## Reproduce

```sh
# Trees: BASELINE = git archive HEAD, CURRENT = a copy of the working tree.
git archive HEAD | tar -x -C /tmp/base
tests/bench/app/laptop-build.sh app-base /tmp/base rel     # or: cmake -DCMAKE_BUILD_TYPE=Release …
nix develop -c tests/bench/app/build-probe.sh /tmp/probe
tests/bench/app/make-bench-vault.py /tmp/vaults/big10k
# Sample notebook: brulion --demo --vault /tmp/vaults/demo --screenshot x.png (seeds it once)
nix shell nixpkgs#xvfb -c env BINS="base=/tmp/base-build/src/app/brulion cur=build-rel/src/app/brulion" \
  PROBE=/tmp/probe/libbenchprobe.so VAULT_DEMO=/tmp/vaults/demo VAULT_BIG=/tmp/vaults/big10k \
  OUT=/tmp/results RUNS=5 tests/bench/app/bench-matrix.sh startup-big typing
tests/bench/app/analyze.py /tmp/results --csv /tmp/results.csv
```

On the Raspberry Pi use `PLATFORM=offscreen` (software renderer), run every group under
`flock /tmp/brulion-build.lock` **inside the dev shell** (`nix develop -c …`: the binaries need its
QML import / plugin paths; outside it Main.qml fails with "module QtQuick.Controls … not found" and
every run exits with code 1 after ~0.3 s) and keep vaults modest (3 000 notes; one run with
v0.1.1 takes ~85 s to settle there, CURRENT ~45 s; 10 000 notes need ~700 MB of RSS).

Attribution (not benchmark numbers: perf's DWARF unwinding slows the app 1.5–3×):

```sh
nix develop -c nix shell nixpkgs#xvfb nixpkgs#perf -c env CUR=build-rel/src/app/brulion \
  QMLDBG=build-qmldbg/src/app/brulion PROBE=/tmp/probe/libbenchprobe.so VAULTS=/tmp/vaults OUT=/tmp/prof \
  tests/bench/app/profile-cases.sh typing-outline startup-big views qml-views
# memory: … nix shell nixpkgs#heaptrack -c … profile-cases.sh heap-startup-big
```

## What the numbers mean

* `startup`: `preload` (the library's constructor: exec + dynamic linking), `qapp`
  (QApplication constructed), `window` (Main.qml created, before `exec()`), `first_frame`,
  `vault_open`, `content` / `content_frame` (the first block on screen, the frame showing it),
  `ready` (`Workspace.ready`: every file indexed), `settled` (the end of the last stall > 24 ms
  before 1 s without one); `indexing_*` / `after_ready_*` stalls between those marks.
  `<mark>_gui_insns_M` / `<mark>_gui_cpu_ms` (window, first_frame, content, ready, settled): the GUI
  thread's user-space instructions and CPU time from the probe's start (QApplication constructed)
  to the mark — on a shared machine compare these rather than the milliseconds;
  `<mark>_objects_alive` / `_objects_created`: the QObjects alive / created since then (QtCore's
  AddQObject / RemoveQObject hooks). `--env BENCH_OBJECT_TYPES=1` adds an `objects` record at
  content and settled: the live objects by class and by their named ancestors under `AppShell`
  (`BENCH_OBJECT_DEPTH`, default 4) — what the shell creates (docs/PERFORMANCE.md "L10").
  `--start first` = a fresh XDG home (first start), `warm` = the same home again, `cold` = also
  the page cache of the binary, every mapped library / font and the vault dropped.
  Index cache (docs/PERFORMANCE.md, L2): a `first` start has no device state, so its index is
  loaded cold (every note parsed on the worker threads); `warm` and `cold` starts reuse the home of
  the warm-up run, whose index cache (`index-cache.bin`, written ~5 s after the index or at the
  latest when the app closes) makes their index warm. `--env BRULION_NO_INDEX_CACHE=1` measures a
  cold index on warm starts, `--env BRULION_INDEX_THREADS=<n>` the parse workers (`0`: in slices on
  the GUI thread, the browser's way). Trees from before wave 5 parse everything on every start.
* `open-page`: `sync` (time inside `AppController.navigate()`), `content` (the page's first
  block exists), `frame` (the frame showing it), `settled`; `1` = first open, `2` = again after
  going home. `*_frame_insns_M` / `*_frame_cpu_ms` and `*_settled_insns_M` / `*_settled_cpu_ms`:
  the GUI thread's user-space instructions (millions) and CPU time from the navigation to that
  frame's sync (read on the render thread while the GUI thread waits for it) and to the end of the
  last stall before settled — the numbers to compare on a shared machine. `BENCH_WAIT_KEPT=1`
  waits for the views built ahead first (on a loaded machine they may still be built while the
  page opens, and their work lands in its numbers); `BENCH_NAV_DELAY_MS` waits before the first
  opening (mark `nav_delay`: time for `perf record -p` attached at `kept_settled` to start).
  Every mark of the GUI thread carries its counters (`gcpu`, `gins`), every frame too (`frames`
  event: `gcpu`, `gins`).
* `typing` (row 10 of the page; key = press + release): `*_handle` = the press's synchronous
  handling, `*_frame` = key press → the frame that shows the change, split into `*_gui` (the
  handling and that frame's sync: the GUI thread's own), `*_wait` (from the handling to the start
  of that sync: the rest of the event-loop turn — the release, polish, timers — and the render
  loop's pacing) and `*_render` (after the sync: the render thread and the swap); `char` (a typed
  letter), `enter`, `tab`, `shift-tab`, `undo` (Ctrl+Z), `redo` (Ctrl+Shift+Z);
  `autosave_blocked` = GUI time blocked by stalls > 12 ms in the 1.7 s after a burst of typing
  (the 400 ms autosave and what follows it), `autosave_blocked_cpu` the GUI thread's CPU time in
  those stalls; the `save_*` / `fanout_*` metrics below.
* `source-typing`: the page in source mode (Ctrl+E: the whole file in one TextEdit), the caret at
  the end of the line in the middle of the file; the bursts and Enters of `typing` and five
  Backspaces, the same metrics (`source-typing-medium`: `Bench/Średnia strona.md`, 32 KB;
  `source-typing-outline5000`: `Bench/Konspekt 5000.md`, 0.5 MB).
* `select-copy` (three rounds on the page: editing row 10, Ctrl+A — the block's text —, Ctrl+A —
  every block, `PageController::selectAll()` —, Ctrl+C — `copySelection()`: the Markdown, its
  HTML and the clipboard —, Escape): `select-blocks_*` and `copy_*` handle / frame like the
  keys of `typing`; `selected_blocks` and `copied_chars` show that it happened.
* `search` / `switcher`: key echo (`*_frame`) and `*_results` (key → the frame after the result
  model changed; the search field debounces 180 ms), `*_query_blocked` = GUI time blocked by
  the query.
* `views` and `graph` activate the window first (`app_active`: an X server without a window
  manager never focuses it, and an inactive app pauses the graph's layout — earlier runs of `views`
  under Xvfb opened a graph that did not lay out).
* `views`: per view and round (`r0` first open, `r1` again): `sync`, `frame`, `settled`; for the
  graph also `frames` (swapped between the navigation and settled) and `frame_p95_ms` /
  `frame_max_ms` (intervals between them).
* `graph` (the 10 000-note vault, its graph capped at 4000 nodes): open the graph, 300 ms later a
  1 s mouse drag on its background (a move every 16 ms) while the layout runs, three wheel zoom
  steps, wait until `GraphModel.running` is false (`layout_ms`) and settled, then another 1 s drag:
  `frame_ms`, `layout_ms`, `settled_ms`, `run_stall_max` (the longest GUI stall while the layout
  runs), `run_frames` / `run_frame_p95_ms` / `run_frame_max_ms`, per drag `*_move_frame` (a move →
  the frame showing it) and `*_frame_p95_ms`, `zoom_frame`.
* `idle`: CPU % of one core, wakeups/s per thread and the timer events that fired, frames/s.
* `journal-scroll` (the 10 000-note vault, the journal from its top): 80 wheel steps down, 40 ms
  apart: `frame_interval_*`, `frames_gt33` / `frames_gt50`, `scroll_stall_*` (GUI stalls between the
  first step and 300 ms after the last), memory and items at `scrolled`. The views built ahead
  (below) are built 1.2 s after `ready`, often during the scroll: `BENCH_WAIT_KEPT=1` waits for them
  first, so that the stalls are the journal's (docs/PERFORMANCE.md "Z10 stage A2"). The "blocks
  shown" of the start-up's `content` count a journal day drawn as one item (`JournalDayItem`) by its
  rows.
* `page-scroll`: `BENCH_PAGE` opened (`open1_*` as `open-page`), then — once it has settled, or
  `BENCH_SCROLL_WAIT` ms after its first block is on screen (`200`: a reader who scrolls at once) —
  80 wheel steps down and 80 up, 40 ms apart: `frame_interval_*`, `frames_gt33` / `frames_gt50`,
  `scroll_stall_*` as `journal-scroll`; memory and items at `page:1` (before) and `scrolled`;
  `scroll_insns_M` (settled runs): the GUI thread's user-space instructions from the first notch to
  500 ms after the last, less its idle rate of 500 ms before it — the number to compare on a
  loaded machine.
* `page-resize`: `BENCH_PAGE` opened and settled, then the window made `BENCH_RESIZE_WIDTH` (920:
  the text column narrower than the 680 px editor width, the sidebar still shown) wide and back
  six times, 600 ms apart: `resize_frame_*` (the request → the frame after the window has its new
  width) and `resize_stall_*` (the longest GUI stall within 500 ms of each request): every row
  that exists lays its text out again; `resize_insns_M_*` the GUI thread's user-space instructions
  in the 500 ms after each request less those of 500 ms of idle before the first resize.
* `open-page`, `page-scroll` (settled runs), `page-resize`: `open<n>_insns_M`, the GUI thread's
  user-space instructions from the navigation until the page has settled (the probe reads the
  thread's counters at both; to the first blocks would compare views that show them before or
  after the rest of the opening's work).
* `switch-pages`: 200 page openings: time to the page's frame, heap / RSS / item count every 20.
* `pictures` (the notebook of `make-picture-vault.py`; docs/PERFORMANCE.md "Pictures and memory"):
  `BENCH_CYCLES` rounds (2) of — the album (`BENCH_PAGE`) opened, wheeled to its end and back;
  every picture of `BENCH_PICTURES` opened in the picture view; every page of `BENCH_PAGES`
  opened — each followed by the journal ("home") and `BENCH_REST_MS` (3000) of rest. A memory mark
  after a garbage collection at every step (`album:<round>`, `album_end:`, `album_back:`,
  `home:after_album:`, `viewer:<round>:<n>`, `home:after_viewer:`, `pages:<round>:<n>`,
  `home:after_pages:`): `rss_mb:<step>`, `anon_mb:` (RssAnon: with llvmpipe under Xvfb the
  textures are the process's memory too), `heap_mb:` (malloc in use + its mapped blocks),
  `pictures:` / `picture_mb:` (the Images that hold a picture and their decoded megabytes: implicit
  size × the screen's ratio² × 4 bytes) and `items:`; `peak_rss_mb` (VmHWM), `grown_*` (the end less
  `home:start`) and `leak_*_per_round` (home after the pages of the last round less the first:
  what a round leaves behind — a leak grows with `BENCH_CYCLES`). `--env BRULION_NEAR_VIEW=0`
  loads every picture wherever its frame is and `--env BRULION_MEMORY_KEEPER=0` gives nothing
  back to the system (the A/B of `NearView` and `MemoryKeeper`); `--env
  QT_LOGGING_RULES=brulion.memory.debug=true` logs every time memory is given back, how much and
  how long it took, `brulion.nearview.debug=true` every picture that comes within reach or leaves.
  `--env BENCH_VIEWPORT_OBSERVERS=1` (any scenario) adds to every memory mark the items that
  observe the viewport, by class — while a scrolled view has one below it, every scroll step walks
  the item tree down to it (docs/PERFORMANCE.md "Pitfalls").
* `memory` (any notebook; docs/PERFORMANCE.md "Memory: only what is in use"): what the app holds
  at each step of a session — `mem:ready`, `mem:settled`, `mem:start` (`BENCH_SETTLE_MS`, 9 s,
  after the start has settled: with whatever the app builds ahead), a tour of the views
  (`mem:view:page` with `BENCH_PAGE`, `tasks`, `calendar`, `allPages`, `search`, `graph`, `tags`,
  `settings`), back at the journal (`mem:home`), after `BENCH_LONG_REST_MS` more in that view
  (`mem:rested`: what goes by itself — the spell checker's dictionaries after 90 s, the hidden
  views after `MemoryPolicy.hiddenViewMs`, which `--env BRULION_HIDDEN_VIEW_MS=20000` shortens),
  then away as with another program in front (`mem:away`), the window hidden (`mem:away_hidden`)
  and shown again, in use (`mem:back`; `MemoryPolicy.setAway()`: trees without it only hide the
  window). Every mark after a garbage collection, with the kernel's own count of the process
  (`/proc/self/smaps_rollup`: `pss_mb:`, `dirty_mb:` — private dirty pages, what only the app
  holds and nothing can drop —, `private_mb:`), malloc's over every arena (`heap_mb:` in use,
  `heap_free_mb:`, `arenas:`; `mallinfo2()` counts the main arena only), `rss_mb:`, `anon_mb:`,
  `file_mb:`, `items:`, `objects:` (QObjects alive), `threads:` and `qml_types:` — the app's QML
  files the engine has loaded (`--env BENCH_QML_TYPES=1` lists them at every mark: diff two marks
  to see what a step loaded). `back_picture` compares the window before it was away with the
  window back from hidden: `back_differing_pixels` (any difference: glyphs land elsewhere in
  their atlas, a few thousand edge pixels) and `back_changed_pixels` (a channel off by more than
  a quarter: something not drawn as before — the title's glyphs, a few hundred; an item that did
  not come back is thousands in one box, `changedBox`); `--env BENCH_AWAY_VIEWS=1` runs the away
  steps in every view of the tour (`back_picture:<view>`), `--env BENCH_PICTURES_DIR=<dir>` saves
  the pictures. `--env BENCH_OBJECT_TYPES=1` adds the live objects by class at `start`, `home` and
  `rested`; `--env BENCH_UNLOAD=1` unloads the hidden kept views and the popups built ahead one
  by one after `start` and marks what each gave back (`mem:unload:<loader>`); `--env BENCH_AWAY=0`
  leaves the away steps out; `--env BRULION_MEMORY_POLICY=0` runs the app as before the policy
  (everything built ahead and kept).

  ```sh
  nix develop -c nix shell nixpkgs#weston -c env WESTON_GPU=pci-0000_00_02_0 tests/bench/app/run-bench.py \
    --bin build-rel/src/app/brulion --probe /tmp/probe/libbenchprobe.so --vault /tmp/vaults/big10k \
    --scenario memory --platform weston --start first --runs 3 --label cur --out /tmp/results/memory \
    --env "BENCH_PAGE=Bench/Konspekt 5000.md" --timeout 600
  ```

  ```sh
  nix shell nixpkgs#imagemagick -c tests/bench/app/make-picture-vault.py /tmp/vaults/photos
  nix develop -c nix shell nixpkgs#xvfb -c tests/bench/app/run-bench.py --bin build-rel/src/app/brulion \
    --probe /tmp/probe/libbenchprobe.so --vault /tmp/vaults/photos --scenario pictures --runs 3 --label cur \
    --out /tmp/results/pictures --timeout 900 --env BENCH_PAGE=Album.md \
    --env BENCH_PAGES=/tmp/vaults/photos.pages.txt --env BENCH_PICTURES=/tmp/vaults/photos.pictures.txt
  ```

## Autosave: where the GUI thread's time goes

A stall needs more than 12 ms between two 4 ms lag ticks, so `autosave_blocked` misses every piece
of work under 12 ms and says nothing about what ran. In the typing windows of `typing` and
`source-typing` (each of the three bursts up to 2.4 s after it: the 400 ms autosave, the save's
end + 1.7 s fits unless a save takes over 300 ms) the probe records, on the GUI thread only:

* **signal spans** (`BENCH_SPANS`: `1` default, `watched` only the list below, `0` none): Qt's
  signal spy callbacks (`qt_register_signal_spy_callbacks`, QtCore's private
  `QSignalSpyCallbackSet`; registered only in the windows) give every emission's begin and end;
  per span the inclusive and self wall time, thread CPU time (`CLOCK_THREAD_CPUTIME_ID`) and
  user-space instructions (a `perf_event_open` counter with `exclude_kernel`, which
  `perf_event_paranoid` 2 allows — on a hybrid Intel CPU one per core PMU, `cpu_core` +
  `cpu_atom`, summed). Watched: `Vault.changed`; `Workspace` / `EditorHost` `revisionChanged`,
  `fileChanged`, `filesChanged`, `fileListChanged`, `indexChanged`; `VaultIndex.changed`;
  `PageController.saved`, `dirtyChanged`; every model's `modelReset`, `layoutChanged`,
  `dataChanged`, `rowsInserted` / `rowsRemoved` / `rowsMoved` (`model:<class>.<signal>`, with the
  rows they name — a reset: the row count after it); `QTimer::timeout` as `timer:<parent's class>`,
  a parentless timer as `timer:<interval>ms(<the first span in it>)` (member timers have no
  parent: `timer:400ms(model:PageListModel.dataChanged)` is All pages' refresh) — `timer:save` is
  the parentless single-shot timer with the controller's `saveDelay` in which a PageController
  emits `saved`, `dirtyChanged` or `saveStarted`, its autosave, so its span is the whole synchronous
  save — or, since the save runs on the writer thread (2026-11-18), only taking the text and sending
  it (`save_merge_ms` below) —, QML
  `Timer`s as `QmlTimer:<parent>:<interval>`; and every other signal of a `brulion::` class whose
  span takes at least `BENCH_SPAN_MIN_US` (200 µs). Self time = inclusive minus the spans nested
  in it. The probe's own timers are left out;
* **file system calls**: the probe interposes `fsync`, `fdatasync`, `rename`, `renameat`,
  `renameat2` and `linkat` (LD_PRELOAD: its definitions come first and call libc's through
  `dlsym(RTLD_NEXT)`), timed on the GUI thread and counted on the others (`other_threads`);
* **the GUI thread's counters at every lag tick** (from 1 s of idle editing before the first burst,
  `idle_ref_start` … `idle_ref_end`, to the end of the last window), and per stall its CPU time and
  instructions (every run, not only typing);
* a mark `kept_views:<burst>` at each burst's start with `n` (the kept views of the main pane that
  exist: ShellRouteView's KeptLoaders — Tasks, Calendar, Journal, All pages, Search — with an
  item), `of` and `loading`: views built ahead 1.2 s after `ready`, one every 0.4 s, may or may not
  exist during the first burst. `BENCH_WAIT_KEPT=1` waits (at most 30 s, mark `kept_ready`) until
  all of them exist and the app has settled before typing (and before `journal-scroll`'s scroll).

What the callbacks see (checked with Qt 6.12 on a test object): `signal_begin` / `signal_end` wrap
every **direct** connection of an emission — string-based (`SIGNAL`/`SLOT`), pointer-to-member and
lambda alike — so a span's inclusive time holds all its direct slots; Qt's `slot_begin` /
`slot_end` fire only for string-based connections (the probe does not use them); a **queued**
connection's slot runs later, outside the span (it shows in `fanout_deferred_cpu_ms`). **QML
handlers and bindings** (`onX:`, `Connections`, bindings re-evaluated by a NOTIFY signal) run
*before* `signal_begin`, through `QAbstractDeclarativeData::signalEmitted`: the probe wraps that
hook too while the spans are on, and records the QML part as a span of the same name with `qml` set
(`(qml)` in the top-N list); `sig_<name>_ms` adds both parts. A span costs a few syscalls (thread
CPU clock, one or two counter reads, ≈ 1–2 µs): `window_emissions` / `window_spans` say how many
emissions the window saw and kept; compare keystroke timings with `BENCH_SPANS=0` when in doubt.

Metrics (analyze.py `save_metrics()`; per burst `<metric>_b<n>`, the run's median of the bursts as
`<metric>`, then medians over runs as usual); the fan-out window runs from the save's start to its
end + 1.7 s (cut at the window's end):

| metric | what |
|---|---|
| `save_sync_ms`, `save_cpu_ms`, `save_insns_M` | the `timer:save` span: the synchronous save (wall, CPU, millions of instructions) — the Zed-like budget is < 8 ms |
| `save_merge_ms`, `save_merge_cpu_ms`, `save_merge_insns_M` | a save in the background (the timer's span holds `PageController.saveStarted`): the merge of its result from the event loop — the outermost `Vault.changed`, `PageController.saved` / `dirtyChanged` spans after the timer (wall, CPU, instructions); the GUI thread's whole save is `save_sync_ms` + `save_merge_ms` |
| `save_write_ms` | the save up to `Vault::changed`: serialising the page, the compare-and-swap read, History, the write |
| `save_fsync_ms` / `save_fsync_n`, `save_rename_ms` | `fsync` + `fdatasync` (and renames) inside the save |
| `save_vault_changed_ms` | `Vault.changed` inside the save: the index update and everything connected directly |
| `sig_<name>_ms` | the watched signals' inclusive time in the fan-out window (C++ + QML parts; nested names overlap) |
| `fanout_deferred_cpu_ms`, `fanout_insns_M` | GUI CPU / instructions from the save's end over 1.7 s minus the idle rate (the median lag-tick rate of the idle reference): the deferred work, also what stays under 12 ms |
| `fanout_model_resets`, `fanout_rows_changed` | model resets + layout changes, and rows named by `dataChanged` / `rows*` in the fan-out window |
| `kept_views` | kept views that existed when the burst started |
| `top<i>_span` | over all bursts' fan-out windows, the spans with the most self time: name, self, inclusive, count |
| `idle_ref_cpu_ms_per_s`, `fanout_window_ms`, `saves`, `window_emissions`, `window_spans` | checks: the idle rate, the window used, saves found, what the spans saw |

`perf-slice.py … --window save --window fanout` slices a perf recording by the same windows (the
probe's spans must be on); `timeline.py --span-ms 0.5` lists the spans in time order with their
nesting.

The core benchmark beside it, `bench_index:saveRoundTrip` (1000 / 3000 / 10 000 notes on disk with
a History folder; "throttled" and "history stored"), times the same save without the GUI:
`writeTextIfUnchanged()` of a 5 KB note → `Vault::changed` → the index update → `changesSince()`;
its scaling limit (k ≤ 0.25, `tests/bench/baselines/limits.csv`) holds a save to O(1) in notebook size.

Caveats: Xvfb + Mesa llvmpipe renders on the CPU (frame costs are higher than on a GPU, idle
frames cost more); the Pi uses the software scene graph renderer. Timings on a shared machine
include noise — take the medians, look at min / max.
