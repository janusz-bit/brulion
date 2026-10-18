#!/usr/bin/env bash
# wasm-batch.sh <dist dir> <label> <out dir> [vault dir] — the browser measurements of one build
# (wasm-bench.mjs in headless Chromium, SwiftShader WebGL), run on the laptop while holding the
# EXCLUSIVE laptop lock. Groups (each its own Chromium processes):
#   <label>-empty      5 first visits (fresh profile: download from localhost, compile, onboarding)
#                      + 5 repeat visits (service worker cache) with empty storage
#   <label>-1k         repeat visits with the 1 000-note vault in IndexedDB (IDBFS load, scan,
#                      journal), IDBFS write-back timings, 3 reloads, 20 s idle
#   <label>-1k-cpu4    the same at 4× CPU slowdown (DevTools throttling ≈ a mid-range phone)
#   <label>-net20-gz / -net20-br   3 first visits at 20 Mbit/s, 40 ms RTT, gzip / brotli bodies
# Environment: LOADS (5), SKIP_NET=1, SKIP_CPU4=1. Node >= 22 (global WebSocket).
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
dist=$1 label=$2 out=$3 vault=${4:-${BENCH_MOBILE_DIR:-$HOME/work/brulion-perf-mobile}/vaults/web1k}
loads=${LOADS:-5}
mkdir -p "$out"
# Chromium profiles on a real disk (IndexedDB timings), not a tmpfs /tmp.
B=(node "$here/wasm-bench.mjs" --dist "$dist" --out "$out" --profile-root "${BENCH_MOBILE_DIR:-$HOME/work/brulion-perf-mobile}/chrome-profiles")
echo "=== $(date +%T) $label: empty storage"
"${B[@]}" --label "$label-empty" --loads "$loads" --visits both
echo "=== $(date +%T) $label: 1 000 notes"
"${B[@]}" --label "$label-1k" --loads "$loads" --visits repeat --vault "$vault" --idbfs --idle 20
if [[ -z ${SKIP_CPU4:-} ]]; then
  echo "=== $(date +%T) $label: 1 000 notes, 4x CPU slowdown"
  "${B[@]}" --label "$label-1k-cpu4" --loads 3 --visits repeat --vault "$vault" --cpu-throttle 4 --timeout 300
fi
if [[ -z ${SKIP_NET:-} ]]; then
  echo "=== $(date +%T) $label: 20 Mbit/s"
  "${B[@]}" --label "$label-net20-gz" --loads 3 --visits first --encoding gzip --throttle 20000,40
  "${B[@]}" --label "$label-net20-br" --loads 3 --visits first --encoding br --throttle 20000,40
fi
echo "=== $(date +%T) $label done"
