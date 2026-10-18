#!/usr/bin/env bash
# CPU profile of one benchmark scenario (perf, DWARF call graphs, CLOCK_MONOTONIC timestamps so
# that the probe's marks and stalls line up with the samples: perf-slice.py).
#   tests/bench/app/perf-scenario.sh <out-prefix> <run-bench.py args…>
# Writes <out-prefix>.data, <out-prefix>.txt (perf script) and the probe's JSONL in
# <out-prefix>-run/. Needs perf (nix shell nixpkgs#perf) and perf_event_paranoid <= 2.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="$1"; shift
freq="${FREQ:-799}"
python3 "$here/run-bench.py" "$@" --runs 1 --start first --out "$out-run" \
  --wrap "perf record -q -k CLOCK_MONOTONIC -F $freq -g --call-graph dwarf,16384 -o $out.data --"
perf script -i "$out.data" -F tid,time,ip,sym,dso > "$out.txt" 2>/dev/null
ls -la "$out.data" "$out.txt"
