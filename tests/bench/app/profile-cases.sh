#!/usr/bin/env bash
# Attribution runs: one scenario under `perf record` (sliced by the probe's windows with
# perf-slice.py) or under qmlprofiler (summarised with qtd-summary.py). Timings of these runs are
# not benchmark numbers (perf's DWARF unwinding slows the app 1.5–3×); they say where time goes.
#
#   CUR=…/brulion QMLDBG=…/brulion-built-with-QT_QML_DEBUG PROBE=libbenchprobe.so VAULTS=vaults \
#   OUT=prof tests/bench/app/profile-cases.sh <case…>
#
# Run inside the dev shell with perf / Xvfb / qmlprofiler on PATH, e.g. on the laptop:
#   nix develop path:. -c nix shell nixpkgs#xvfb nixpkgs#perf -c tests/bench/app/profile-cases.sh typing-outline
# (qmlprofiler comes with the dev shell's Qt). VAULTS holds big10k/ and demo/ (make-bench-vault.py,
# `brulion --demo`). BIN overrides CUR for the perf cases (BIN=…/v0.1.1/brulion LABEL=base).
#
# perf cases:  startup-demo startup-big typing-small typing-outline typing-document open-outline
#              open-document views search switcher journal-scroll switch-pages idle-edit
# qml cases:   qml-startup-demo qml-startup-big qml-open-outline qml-views qml-jscroll qml-typing
#              qml-switcher qml-search
# memory:      heap-startup-big (heaptrack on PATH: nix shell nixpkgs#heaptrack)
# Output per case: OUT/<case>-<label>.{data,txt,slice.txt} and the probe's run in OUT/<case>-<label>-run/;
# qml cases: OUT/<case>.qtd and OUT/<case>.qtd.txt.
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
: "${PROBE:?}" "${VAULTS:?}" "${OUT:?}"
bin="${BIN:-${CUR:-}}"
label="${LABEL:-cur}"
top="${TOP:-45}"
mkdir -p "$OUT"
big="$VAULTS/big10k"
pages="$(realpath "$VAULTS/big10k.pages.txt")"

perf_case() { # perf_case <case> <slice windows…> -- <run-bench args…>
  local c="$1"; shift
  local windows=()
  while [[ $# -gt 0 && "$1" != "--" ]]; do windows+=(--window "$1"); shift; done
  shift
  local p="$OUT/$c-$label"
  rm -rf "$p-run"
  bash "$here/perf-scenario.sh" "$p" --bin "$bin" --probe "$PROBE" --label "$label" --name "$c" \
    --timeout 900 --env BENCH_TIMEOUT_MS=900000 "$@" || { echo "profile: $c failed" >&2; return 1; }
  python3 "$here/perf-slice.py" "$p.txt" "$p-run/$c-$label-00.jsonl" "${windows[@]}" --top "$top" > "$p.slice.txt"
  echo "profile: $p.slice.txt"
}

qml_case() { # qml_case <case> <run-bench args…>
  local c="$1"; shift
  : "${QMLDBG:?QMLDBG= a build with -DQT_QML_DEBUG}"
  rm -rf "$OUT/$c.qtd" "$OUT/$c-run"
  python3 "$here/run-bench.py" --bin "$QMLDBG" --probe "$PROBE" --label qmldbg --name "$c" --runs 1 --start first \
    --out "$OUT/$c-run" --timeout 900 --env BENCH_TIMEOUT_MS=900000 \
    --wrap "qmlprofiler -o $(realpath "$OUT")/$c.qtd --include javascript,compiling,creating,binding,handlingsignal --" \
    --wrap-env "$@"
  python3 "$here/qtd-summary.py" "$OUT/$c.qtd" --top 60 > "$OUT/$c.qtd.txt"
  echo "profile: $OUT/$c.qtd.txt"
}

for c in "$@"; do
  case $c in
    startup-demo) perf_case $c "mark:qapp..window" "mark:window..first_frame" "mark:first_frame..settled" -- \
        --vault "$VAULTS/demo" --scenario startup ;;
    startup-big) perf_case $c "mark:qapp..window" "mark:window..first_frame" indexing "stalls:300@ready..settled" after_ready -- \
        --vault "$big" --scenario startup ;;
    typing-small) perf_case $c keys autosave -- --vault "$big" --scenario typing \
        --env "BENCH_PAGE=Bench/Mała strona.md" --env BENCH_ROW=3 ;;
    typing-outline) perf_case $c keys autosave -- --vault "$big" --scenario typing --env "BENCH_PAGE=Bench/Konspekt 5000.md" ;;
    typing-document) perf_case $c keys autosave -- --vault "$big" --scenario typing --env "BENCH_PAGE=Bench/Dokument 5000.md" ;;
    open-outline) perf_case $c "mark:nav_start:1..page_content:1" "mark:page_content:1..page_settled:1" \
        "mark:nav_start:2..page_settled:2" -- --vault "$big" --scenario open-page --env "BENCH_PAGE=Bench/Konspekt 5000.md" ;;
    open-document) perf_case $c "mark:nav_start:1..page_content:1" "mark:page_content:1..page_settled:1" -- \
        --vault "$big" --scenario open-page --env "BENCH_PAGE=Bench/Dokument 5000.md" ;;
    views) perf_case $c "mark:nav_start:tasks:0..view_settled:tasks:0" "mark:nav_start:calendar:0..view_settled:calendar:0" \
        "mark:nav_start:agenda:0..view_settled:agenda:0" "mark:nav_start:graph:0..view_settled:graph:0" \
        "mark:nav_start:allPages:0..view_settled:allPages:0" "mark:nav_start:tags:0..view_settled:tags:0" \
        "mark:nav_start:journals-after-tasks:0..view_settled:journals-after-tasks:0" -- --vault "$big" --scenario views ;;
    search) perf_case $c keys "stalls:30@phase:slow..search_done" -- --vault "$big" --scenario search ;;
    switcher) perf_case $c keys "mark:switcher_open:0..switcher_focus:0" -- --vault "$big" --scenario switcher ;;
    journal-scroll) perf_case $c "mark:scroll_start..scroll_settled" "stalls:30@scroll_start..scroll_settled" -- \
        --vault "$big" --scenario journal-scroll ;;
    switch-pages) perf_case $c "mark:nav_start:20..page_content:199" -- --vault "$big" --scenario switch-pages \
        --env "BENCH_PAGES=$pages" ;;
    idle-edit) perf_case $c "mark:idle_start:editing..idle_end:editing" -- --vault "$big" --scenario idle-edit \
        --env BENCH_IDLE_MS=10000 --env "BENCH_PAGE=Bench/Mała strona.md" ;;
    qml-startup-demo) qml_case $c --vault "$VAULTS/demo" --scenario startup ;;
    qml-startup-big) qml_case $c --vault "$big" --scenario startup ;;
    qml-open-outline) qml_case $c --vault "$big" --scenario open-page --env "BENCH_PAGE=Bench/Konspekt 5000.md" ;;
    qml-views) qml_case $c --vault "$big" --scenario views ;;
    qml-jscroll) qml_case $c --vault "$big" --scenario journal-scroll ;;
    qml-typing) qml_case $c --vault "$big" --scenario typing --env "BENCH_PAGE=Bench/Konspekt 5000.md" ;;
    qml-switcher) qml_case $c --vault "$big" --scenario switcher ;;
    qml-search) qml_case $c --vault "$big" --scenario search ;;
    heap-startup-big)
      # Who holds the memory once a 10 000-note notebook is open (heaptrack: nix shell nixpkgs#heaptrack).
      # The probe stays in LD_PRELOAD next to heaptrack's own library (--wrap-env). Offscreen + the
      # software renderer: GLX does not come up under heaptrack (and GL driver memory is not ours).
      rm -rf "$OUT/$c-run" "$OUT/$c".*
      python3 "$here/run-bench.py" --bin "$bin" --probe "$PROBE" --label "$label" --name "$c" --runs 1 --start first \
        --vault "$big" --scenario startup --out "$OUT/$c-run" --timeout 1200 --env BENCH_TIMEOUT_MS=1200000 \
        --platform offscreen --wrap "heaptrack --record-only -o $(realpath "$OUT")/$c" --wrap-env
      f=$(ls "$OUT/$c".*.zst "$OUT/$c".zst "$OUT/$c".*.gz 2>/dev/null | head -1)
      heaptrack_print -f "$f" --print-peaks 1 --peak-limit 40 --print-allocators 0 --print-leaks 0 \
        --print-temporary 0 --print-histogram 0 --print-flamegraph 0 --print-massif 0 > "$OUT/$c.heap.txt" 2>&1
      echo "profile: $OUT/$c.heap.txt" ;;
    *) echo "profile-cases: unknown case $c" >&2 ;;
  esac
done
