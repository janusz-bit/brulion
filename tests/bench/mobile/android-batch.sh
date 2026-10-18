#!/usr/bin/env bash
# android-batch.sh <batch> <apk tag> <label> [out dir] — one measurement window on the benchmark
# emulator (boots it, waits until the guest is quiet, measures, shuts it down). Run on the
# laptop while holding the EXCLUSIVE laptop lock (laptop-window.sh exclusive …). The vaults must
# be on the device already (android-measure.sh push-vault …, open --demo).
#   probe    debuggable measurement APK: probe startup on the demo vault and the 5 000-note vault,
#            open-page (5000-block outline), views (Tasks/Calendar/Agenda/Graph/…, memory after),
#            idle (journal left alone 20 s: CPU, wakeups and timers per thread)
#   release  the release APK as a user starts it from the launcher: warm / hot / cold (page cache
#            dropped) starts on the demo vault, warm / cold on the 5 000-note vault; PSS after each
#            start; memory after opening the big pages; idle in the foreground and background
#   Environment: RUNS (5), SKIP_* to leave parts out (SKIP_DEMO=1, SKIP_BIG=1, SKIP_IDLE=1, …)
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
batch=$1 tag=$2 label=$3 dir=${4:-android}
runs=${RUNS:-5}
M="$here/android-measure.sh"
PKG=io.github.januszbit.brulion
F=/data/user/0/$PKG/files
DEMO="$F/Przykładowy brulion"
BIG=$F/big5k
trap '"$here/emu.sh" stop' EXIT
"$here/emu.sh" start
sleep 5
"$here/emu.sh" quiet "${QUIET_LOAD:-2.0}"
"$M" install "$tag"
echo "=== $(date +%T) batch $batch $tag ($label), guest load $("$here/emu.sh" shell cat /proc/loadavg | tr -d '\r')"
case $batch in
  probe)
    if [[ -z ${SKIP_DEMO:-} ]]; then
      "$M" use-vault "$DEMO"
      PROBE_TIMEOUT=120 "$M" probe "$dir" "$label" startup-demo "$runs" startup
    fi
    if [[ -z ${SKIP_BIG:-} ]]; then
      "$M" use-vault "$BIG"
      PROBE_TIMEOUT=300 "$M" probe "$dir" "$label" startup-big5k "$runs" startup
    fi
    if [[ -z ${SKIP_PAGE:-} ]]; then
      "$M" use-vault "$BIG"
      PROBE_TIMEOUT=300 "$M" probe "$dir" "$label" open-page-big5k 3 open-page "BENCH_PAGE=Bench/Konspekt 5000.md"
    fi
    if [[ -z ${SKIP_VIEWS:-} ]]; then
      "$M" use-vault "$BIG"
      KEEP_MEMINFO=1 PROBE_TIMEOUT=400 "$M" probe "$dir" "$label" views-big5k 3 views BENCH_KEEP=1
    fi
    if [[ -z ${SKIP_IDLE:-} ]]; then
      "$M" use-vault "$DEMO"
      PROBE_TIMEOUT=200 "$M" probe "$dir" "$label" idle-demo 2 idle BENCH_IDLE_MS=20000
    fi
    ;;
  release)
    if [[ -z ${SKIP_DEMO:-} ]]; then
      "$M" use-vault "$DEMO"
      "$M" startup "$dir" "$label-demo" warm "$runs" 10
      "$M" startup "$dir" "$label-demo" hot "$runs" 6
      "$M" startup "$dir" "$label-demo" cold "$runs" 10
    fi
    if [[ -z ${SKIP_IDLE:-} ]]; then
      "$M" use-vault "$DEMO"
      "$here/emu.sh" shell am start -W -n "$PKG/$PKG.android.BrulionActivity" >/dev/null
      sleep 15
      "$M" idle "$dir" "$label-demo" 30 fg
      "$M" idle "$dir" "$label-demo" 30 bg
      "$M" meminfo "$dir" "$label-demo-after-idle"
    fi
    if [[ -z ${SKIP_BIG:-} ]]; then
      "$M" use-vault "$BIG"
      "$M" startup "$dir" "$label-big5k" warm "$runs" 25
      "$M" startup "$dir" "$label-big5k" cold 3 25
      # After navigation: the 5000-block outline and document (notification-tap extra opens a page).
      "$M" meminfo "$dir" "$label-big5k-journal"
      "$M" nav "Bench/Konspekt 5000.md"
      sleep 20
      "$M" meminfo "$dir" "$label-big5k-outline5000"
      "$M" nav "Bench/Dokument 5000.md"
      sleep 20
      "$M" meminfo "$dir" "$label-big5k-document5000"
      "$here/emu.sh" shell input keyevent 4 # back
      sleep 10
      "$M" meminfo "$dir" "$label-big5k-back"
      "$here/emu.sh" adb exec-out screencap -p >"${BENCH_MOBILE_DIR:-$HOME/work/brulion-perf-mobile}/out/$dir/$label-big5k-back.png" || true
    fi
    ;;
  *) echo "android-batch: unknown batch $batch" >&2; exit 2 ;;
esac
"$here/emu.sh" adb logcat -d >"${BENCH_MOBILE_DIR:-$HOME/work/brulion-perf-mobile}/out/$dir/logcat-$batch-$label.txt" || true
echo "=== $(date +%T) batch done"
