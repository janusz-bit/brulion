# Mobile benchmarks: Android (emulator) and the browser (WebAssembly)

These tools measure the Android APK and the WebAssembly site from the outside. Product code is not changed. The Android timing probe is the app benchmark probe (`../app/probe/benchprobe.cpp`). It ships only in a debuggable **measurement copy** of the source tree, never in a shipped APK. Everything runs on the x86_64 laptop. Hold the laptop lock shared for builds and exclusive for emulator or browser timing windows (`laptop-window.sh`).

| File | What |
|---|---|
| `android-apk.sh <tree> <tag> <abi> [rel\|dbg]` | One APK variant via `scripts/android-build.sh`: `rel` is Release, signed with the throwaway key (the release keystore variables are unset); `dbg` is the measurement copy. Writes `$W/apk/<tag>.apk` plus the unstripped `libbrulion` |
| `android-probe/measure-copy.sh` | Makes the measurement copy: `android:debuggable="true"`, and the probe as `libbenchprobe.so` via `QT_ANDROID_EXTRA_LIBS` (Qt's loader loads it before `libbrulion`) |
| `android-probe/probe_android.cpp` | Shim: `BENCH_T0` = process start (`/proc/self/stat`), `BENCH_TARGET=libbrulion` (on Android `applicationFilePath()` is the main library), logcat tag `benchprobe` |
| `emu.sh create\|start\|stop\|quiet\|status\|adb\|shell` | The benchmark AVD: API 36 google_apis x86_64, pixel_6 (1080×2400, 420 dpi), 4 vCPU, 4 GB, port 5582, SwiftShader. The emulator lives inside one ssh session (steam-run's bwrap dies with its parent) |
| `android-measure.sh` | `install`, `push-vault` (root adb, owner + SELinux label of the app dir), `use-vault` (edits the settings file, because release APKs ignore `applicationArguments`), `open` (debuggable: `--demo`/`--vault`), `startup` (launcher starts: `am start -W`, "Displayed", `dumpsys meminfo`, CPU), `probe` (JSONL + `.runs.json` for `../app/analyze.py`), `meminfo`, `idle` (per-thread CPU/wakeups from `/proc`, `dumpsys alarm`/`jobscheduler`), `nav` (notification-tap extra opens a page) |
| `android-batch.sh probe\|release <tag> <label>` | One measurement window: boot, wait for a quiet guest, measure, stop |
| `android-summary.py <dir>` | Tables: launcher starts, memory snapshots, idle |
| `size-report.py apk\|syms\|objs` | APK entries by category and the top 20 (stored vs deflated), the final library's symbols by category (QML AOT/units, qrc, moc, core, …), object files by group |
| `wasm-bench.mjs` | Headless Chromium over CDP with its own static server (optionally gzip/br bodies and throttling). Hooks: streaming compile, module ready, the shell's `storage`/`ui` stages, `done()` and `indexedAt` (`index_ready_ms`: every note indexed; builds since N13), WebGL draws, long tasks, main-thread busy time (`Performance.getMetrics`), wasm memory, renderer/GPU RSS. Can seed IndexedDB the way IDBFS stores files (`--vault`); IDBFS write-back timings (`--idbfs`); idle CPU (`--idle`) with screenshots before and after a forced repaint |
| `wasm-batch.sh <dist> <label> <out>` | The browser matrix: empty storage (first and repeat visits), 1 000 notes (+ IDBFS, idle), 4× CPU slowdown, 20 Mbit/s with gzip and with brotli. Since wave 5 the app keeps an index cache in IndexedDB, written ~5 s after the first visit's index is ready (`brulionShell.indexCacheSavedAt`, then the IDBFS write-back); wasm-bench closes the page sooner, so its repeat visits are cold unless `wasm-bench.mjs --wait-cache 60` waits for the cache after the first load (`indexed_from_cache` in the results says how many notes came from it). Android likewise keeps it in the app's data (the first start after an install or an update is cold) |
| `wasm-summary.py <dir>` | Markdown tables (labels side by side) |
| `wasm-linkmap.sh` + `wasm-linkmap.py` | A `MinSizeRel` wasm build like `scripts/wasm-build.sh` plus `-Wl,-Map`. Attributes code and data to Qt modules, plugins, unused styles, the app's QML cache, qrc, core modules… (sizes before wasm-opt) |
| `wasm-sections.py` | Sections and the largest data segments of a `.wasm` (no name section needed) |
| `laptop-window.sh exclusive\|shared <log> '<cmd>'` | Runs a snippet on the laptop under the lock (`LAPTOP_LOCK`) |

## Reproduce

```sh
W=~/work/brulion-perf-mobile   # on the laptop; T=$W/tools/bench/mobile (rsync tests/bench there)
git archive HEAD    | ssh laptop "tar -x -C $W/cur"     # CURRENT
git archive f61a2a4 | ssh laptop "tar -x -C $W/base"    # BASELINE v0.1.1
export LAPTOP_LOCK=<lock file>
# APKs (shared lock)
tests/bench/mobile/laptop-window.sh shared b.log '$T/android-apk.sh $W/cur cur-rel-x86_64 x86_64 rel && $T/android-apk.sh $W/cur cur-rel-arm64 arm64-v8a rel && $T/android-apk.sh $W/cur cur-dbg-x86_64 x86_64 dbg'
# (same for base); vaults
python3 tests/bench/app/make-bench-vault.py $W/vaults/big5k --notes 5000 --today <emulator date>
python3 tests/bench/app/make-bench-vault.py $W/vaults/web1k --notes 1000 --days 240 --big-blocks 1000 --today <date>
# emulator setup (exclusive): AVD, demo vault, 5k vault
laptop-window.sh exclusive s.log 'nix shell nixpkgs#jdk21_headless -c $T/emu.sh create; trap "$T/emu.sh stop" EXIT; $T/emu.sh start; $T/emu.sh quiet 2.5; $T/android-measure.sh install cur-dbg-x86_64; $T/android-measure.sh push-vault $W/vaults/big5k big5k; $T/android-measure.sh open --demo'
# measurement windows (exclusive, one per call)
laptop-window.sh exclusive p.log 'SKIP_PAGE=1 SKIP_VIEWS=1 SKIP_IDLE=1 $T/android-batch.sh probe cur-dbg-x86_64 cur'
laptop-window.sh exclusive p.log 'SKIP_DEMO=1 SKIP_BIG=1 $T/android-batch.sh probe cur-dbg-x86_64 cur'
laptop-window.sh exclusive r.log '$T/android-batch.sh release cur-rel-x86_64 cur'
python3 tests/bench/app/analyze.py $W/out/android; python3 tests/bench/mobile/android-summary.py $W/out/android --md
# browser
(cd $W/cur && BUILD_DIR=$W/wasm-build-cur DIST_DIR=$W/wasm-dist-cur scripts/wasm-build.sh)     # shared lock
laptop-window.sh exclusive w.log '$T/wasm-batch.sh $W/wasm-dist-cur cur $W/out/wasm'
python3 tests/bench/mobile/wasm-summary.py $W/out/wasm
$T/wasm-linkmap.sh $W/cur $W/wasm-build-cur-map $W/out/wasm-cur.map && python3 $T/wasm-linkmap.py $W/out/wasm-cur.map
```

## Caveats

* `emu.sh stop` runs `sync` in the guest before `emu kill`: the kill is a power cut, and files written shortly before it (a vault pushed with `push-vault`, the app's saves) came back as empty files on the next boot — measurements then ran on an empty vault without any error. `push-vault` also syncs and fails when files arrive empty.
* Emulator: x86_64 under KVM on the laptop's cores (per-core faster than phone ARM cores), SwiftShader GL (frames are rendered on the CPU, GPU memory is not counted in PSS), host SSD storage, no thermal limits. Treat it for relative comparisons (BASELINE vs CURRENT, before vs after a change), not for phone absolutes.
* The probe needs the debuggable copy. Debuggable ART adds about 100 ms to `am start -W` (390 vs 287 ms). Startup marks are ms since the zygote fork (10 ms resolution).
* `am start -W` TotalTime / "Displayed" stops at the activity's first frame (Qt's surface or splash), not Qt content. Use the probe's `first_frame`/`content_frame` for those.
* Browser: headless Chromium, SwiftShader WebGL, localhost. `--cpu-throttle 4` is DevTools CPU throttling (main thread only).
