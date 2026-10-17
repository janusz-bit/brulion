#!/usr/bin/env bash
# Scroll measurements on an Android phone or emulator with real touch input: starts the installed
# Brulion with the scroll probe in observe mode (src/app/ui/scrollprobe.h), scrolls with
# `adb shell input swipe` (slow drags, flings, down and up) and pulls the probe's CSV and summary.
#
#   tests/app/perf/android-scroll.sh [name]
#
# Needs a DEBUGGABLE APK with the probe: Qt passes the `applicationArguments` intent extra only to
# those (build one with android:debuggable="true" on <application> in android/AndroidManifest.xml;
# do not ship it), and a Release build leaves the probe out (BRULION_ANDROID_BUILD_TYPE=RelWithDebInfo
# scripts/android-build.sh, or -DBRULION_SCROLL_PROBE=ON).
# The files are read with `run-as`, so no root is needed.
#
# Environment: ADB (default adb), ANDROID_SERIAL (adb's device), ROUTE (default journals), PAGE (a
# page to open, e.g. "Witaj w Brulionie.md"), APP_ARGS (more app arguments; Qt splits at spaces),
# SWIPES ("dir:ms[:fraction]" — dir down|up, the stroke's duration and its length as a fraction
# of the screen height; default: slow drags, medium drags, flings, both ways), MAX_MS (recording
# time, default 45000), OUT_DIR (default tests/screenshots/perf/android).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.."

name="${1:-android-$(date +%H%M%S)}"
adb="${ADB:-adb}"
pkg=io.github.januszbit.brulion
out="${OUT_DIR:-tests/screenshots/perf/android}"
max="${MAX_MS:-45000}"
read -r -a swipes <<< "${SWIPES:-down:1500:0.5 down:800:0.5 down:300:0.5 down:100:0.25 down:100:0.25 up:100:0.25 up:100:0.25 up:300:0.5 up:1500:0.5}"
mkdir -p "$out"

"$adb" shell run-as "$pkg" true 2>/dev/null ||
  { echo "android-scroll: $pkg is not installed or not debuggable (run-as fails)" >&2; exit 1; }
size=$("$adb" shell wm size | sed -nE 's/.*: ([0-9]+)x([0-9]+).*/\1 \2/p' | tail -n 1)
read -r width height <<< "$size"
x=$((width / 2)) mid=$((height * 9 / 16))

args="--scroll-probe auto --scroll-script observe --probe-wait 2000 --probe-max $max"
args+=" --probe-out /data/user/0/$pkg/files/$name.csv --route ${ROUTE:-journals} ${APP_ARGS:-}"
extra=()
[[ -n "${PAGE:-}" ]] && extra=(--es "$pkg.android.extra.PATH" "'$PAGE'")
"$adb" shell am force-stop "$pkg"
"$adb" shell am start -W -n "$pkg/.android.BrulionActivity" --es applicationArguments "'$args'" "${extra[@]}" >/dev/null
sleep 6

for s in "${swipes[@]}"; do
  IFS=: read -r dir ms fraction <<< "$s"
  half=$(awk -v h="$height" -v f="${fraction:-0.5}" 'BEGIN { printf "%d", h * f / 2 }')
  if [[ "$dir" == down ]]; then y1=$((mid + half)) y2=$((mid - half)); else y1=$((mid - half)) y2=$((mid + half)); fi
  echo "android-scroll: swipe $dir ${ms} ms, $((2 * half)) px" >&2
  "$adb" shell input swipe "$x" "$y1" "$x" "$y2" "$ms"
  sleep 2
done

# The probe quits the app when its time is up and writes <name>.csv and <name>.csv.log.
for _ in $(seq 1 $((max / 1000 + 30))); do
  "$adb" shell run-as "$pkg" ls "files/$name.csv.log" >/dev/null 2>&1 && break
  sleep 1
done
sleep 1
"$adb" exec-out run-as "$pkg" cat "files/$name.csv" > "$out/$name.csv"
"$adb" exec-out run-as "$pkg" cat "files/$name.csv.log" > "$out/$name.log"
grep -E "^scroll-probe: (target|tuning|SUMMARY|FRAMES|JUMPS|VIEW|DELEGATES|TOUCH|  stroke)" "$out/$name.log" >&2 || true
echo "android-scroll: $out/$name.csv, $out/$name.log" >&2
