#!/usr/bin/env bash
# CPU profile of one part of a scenario only: run the scenario (run-bench.py, one run), wait until
# the probe writes <mark> (e.g. "settled", "scroll_start"), then attach `perf record` to the app for
# <seconds>. For slow machines (the Raspberry Pi), where recording a whole run with DWARF call graphs
# would be too big; perf-slice.py works on the result like on perf-scenario.sh's.
#   tests/bench/app/perf-attach.sh <out-prefix> <mark> <seconds> <run-bench.py args…>
# Writes <out-prefix>.data, <out-prefix>.txt (perf script), the probe's run in <out-prefix>-run/ and
# <out-prefix>.threads.txt (samples per thread and DSO for the window: render thread, GUI thread…).
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="$1" mark="$2" secs="$3"; shift 3
freq="${FREQ:-249}"
rm -rf "$out-run" "$out.data"
python3 "$here/run-bench.py" "$@" --runs 1 --start first --out "$out-run" &
runner=$!
jsonl=""
pid=""
while kill -0 "$runner" 2>/dev/null; do
  [[ -n "$jsonl" ]] || jsonl=$(ls "$out-run"/*-00.jsonl 2>/dev/null | head -1 || true)
  if [[ -n "$jsonl" && -z "$pid" ]]; then
    pid=$(grep -m1 '"ev":"start"' "$jsonl" 2>/dev/null | sed -n 's/.*"pid":\([0-9]*\).*/\1/p' || true)
  fi
  if [[ -n "$pid" ]] && grep -q "\"name\":\"$mark\"" "$jsonl" 2>/dev/null; then
    perf record -q -k CLOCK_MONOTONIC -F "$freq" -g --call-graph dwarf,8192 -p "$pid" -o "$out.data" -- sleep "$secs" || true
    break
  fi
  sleep 0.05
done
wait "$runner" || true
[[ -f "$out.data" ]] || { echo "perf-attach: mark $mark never came" >&2; exit 1; }
perf script -i "$out.data" -F tid,time,ip,sym,dso > "$out.txt" 2>/dev/null
perf report -i "$out.data" --no-children --sort comm,dso --stdio 2>/dev/null | grep -v '^#' | grep -v '^$' | head -40 > "$out.threads.txt"
ls -la "$out.data" "$out.txt"
