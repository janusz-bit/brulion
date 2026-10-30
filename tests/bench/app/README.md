# App benchmarks (desktop and ARM)

Measurements of the real `brulion` binary from the outside — no hook in the product code, so the
same scripts measure any build (v0.1.1 and the working tree alike). An `LD_PRELOAD` library
(`probe/benchprobe.cpp`) rides along in the app's process: it records every frame (render thread
`beforeSynchronizing` → `frameSwapped`), GUI-thread stalls (a 4 ms precise timer; a gap > 12 ms
is a stall — the time input would have waited), memory (`/proc/self/status`, malloc in use) and
CPU, and drives the app through a scenario: real input through `QWindowSystemInterface` (keys
with shortcuts, wheel), navigation through the `AppController` singleton, and the editor through
the `pageEditor` / `pageController` objects.

| File | What |
|---|---|
| `probe/benchprobe.cpp` | the probe (scenarios: `startup`, `open-page`, `typing`, `select-copy`, `search`, `switcher`, `views`, `graph`, `journal-scroll`, `switch-pages`, `idle`, `idle-edit`, `workload`; see the header) |
| `build-probe.sh <dir>` | builds `libbenchprobe.so` against the dev shell's Qt (a throw-away CMake project in `<dir>`) |
| `make-bench-vault.py <dir>` | a deterministic synthetic notebook: 10 000 notes (≈ 650 journal days over two years + pages in 40 folders, links, tags, tasks with dates and reminders, a few queries), `Bench/Konspekt 5000.md` (5000-block outline), `Bench/Dokument 5000.md` (5000-block document), `Bench/Mała strona.md`; `<dir>.pages.txt` lists 50 pages for page switching |
| `run-bench.py` | runs one scenario N times (fresh working copy of the vault per run; `--start first\|warm\|cold`; Xvfb or offscreen); writes one JSONL per run |
| `bench-matrix.sh` | the whole matrix for several labelled builds (`BINS="base=… cur=…"`), group by group (`startup-big-short`: first + warm starts only, for slow machines) |
| `analyze.py <dir>…` | metrics per run → median / min / max / p25 / p75 per metric and label (`--csv`, `--json`) |
| `timeline.py run.jsonl` | one run's events in time order (marks, input, stalls, model signals, frame gaps) |
| `perf-scenario.sh` + `perf-slice.py` | a scenario under `perf record` (CLOCK_MONOTONIC) and the GUI thread's samples sliced by the probe's windows (`after_ready`, `stalls:100@content:ready`, `autosave`, `keys`, …) |
| `perf-attach.sh` | `perf record -p` attached only once the probe writes a given mark, for N seconds (slow machines: the Pi), then `perf script` + per-thread/DSO summary |
| `profile-cases.sh` | named attribution runs: perf cases (`typing-outline`, `startup-big`, `views`, `switcher`, …: `perf-scenario.sh` + `perf-slice.py` with the right windows), qmlprofiler cases (`qml-*`) and heaptrack (`heap-startup-big`) |
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
  going home.
* `typing` (row 10 of the page; key = press + release): `*_handle` = the press's synchronous
  handling, `*_frame` = key press → the frame that shows the change; `char` (a typed letter),
  `enter`, `tab`, `shift-tab`, `undo` (Ctrl+Z), `redo` (Ctrl+Shift+Z); `autosave_blocked` = GUI
  time blocked in the 1.7 s after a burst of typing (the 400 ms autosave and what follows it).
* `select-copy` (three rounds on the page: editing row 10, Ctrl+A — the block's text —, Ctrl+A —
  every block, `PageController::selectAll()` —, Ctrl+C — `copySelection()`: the Markdown, its
  HTML and the clipboard —, Escape): `select-blocks_*` and `copy_*` handle / frame like the
  keys of `typing`; `selected_blocks` and `copied_chars` show that it happened.
* `search` / `switcher`: key echo (`*_frame`) and `*_results` (key → the frame after the result
  model changed; the search field debounces 180 ms), `*_query_blocked` = GUI time blocked by
  the query.
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
* `switch-pages`: 200 page openings: time to the page's frame, heap / RSS / item count every 20.

Caveats: Xvfb + Mesa llvmpipe renders on the CPU (frame costs are higher than on a GPU, idle
frames cost more); the Pi uses the software scene graph renderer. Timings on a shared machine
include noise — take the medians, look at min / max.
