#!/usr/bin/env bash
# laptop-window.sh exclusive|shared <log> <commands…> — runs a shell snippet on the laptop (in
# $BENCH_MOBILE_DIR, default ~/work/brulion-perf-mobile, with T = its tools/bench/mobile) while
# holding the machine-wide laptop lock on this machine: exclusive for emulator / timing windows,
# shared for builds. The lock file is $LAPTOP_LOCK. Prints and appends the output to <log>.
#   LAPTOP_LOCK=/path/laptop.lock tests/bench/mobile/laptop-window.sh exclusive run.log '$T/emu.sh status'
set -euo pipefail
mode=$1 log=$2
shift 2
lock=${LAPTOP_LOCK:?set LAPTOP_LOCK to the laptop lock file}
flag=-s
[[ $mode == exclusive ]] && flag=-x
{
  echo "=== $(date +%T) waiting for the $mode lock"
  flock "$flag" "$lock" bash -c '
    echo "=== $(date +%T) lock held"
    ssh -o BatchMode=yes -o ServerAliveInterval=30 laptop "W=\${BENCH_MOBILE_DIR:-\$HOME/work/brulion-perf-mobile}; T=\$W/tools/bench/mobile; cd \$W; export PATH=\$PATH; set -e; $1"
    rc=$?
    echo "=== $(date +%T) lock released (exit $rc)"
    exit $rc
  ' _ "$*"
} 2>&1 | tee -a "$log"
exit "${PIPESTATUS[0]}"
