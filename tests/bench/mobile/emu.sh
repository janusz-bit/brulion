#!/usr/bin/env bash
# emu.sh create|start|stop|status|quiet|shell … — the Android emulator of the mobile benchmarks
# (x86_64 Linux host with KVM, the laptop). Its own AVD and port, so it never meets the emulator
# of scripts/android-emulator-test.sh or other agents.
#   create        the AVD (API 36 google_apis x86_64, pixel_6 profile: 1080×2400, 420 dpi; 4 cores,
#                 4 GB RAM) from the SDK of scripts/android-build.sh
#   start [gpu]   boots it headless (default gpu swiftshader_indirect), waits for boot, adb root,
#                 screen kept on, keyguard off; WIPE=1 erases the user data first
#   stop          shuts it down
#   quiet [load]  waits (≤ 10 min) until the guest's 1-min load average is below load (default 1.0):
#                 after boot the system dexopts and syncs for minutes and distorts every timing
#   status        boot state, load, memory
#   adb …         adb on this emulator
# Environment: ANDROID_SDK_ROOT (~/.cache/brulion-android-sdk), BENCH_AVD (brulion-perf-mobile),
# BENCH_EMU_PORT (5582; adb only scans 5555–5585), BENCH_MOBILE_DIR (~/work/brulion-perf-mobile: logs, pid).
# On NixOS the emulator runs under steam-run (its bundled libraries expect an FHS system).
set -euo pipefail
SDK=${ANDROID_SDK_ROOT:-$HOME/.cache/brulion-android-sdk}
export ANDROID_SDK_ROOT=$SDK ANDROID_HOME=$SDK
export ANDROID_USER_HOME=${ANDROID_USER_HOME:-$HOME/.cache/brulion-android-user}
export ANDROID_AVD_HOME=$ANDROID_USER_HOME/avd
IMAGE=${BENCH_EMU_IMAGE:-system-images;android-36;google_apis;x86_64}
AVD=${BENCH_AVD:-brulion-perf-mobile}
PORT=${BENCH_EMU_PORT:-5582}
SERIAL=emulator-$PORT
W=${BENCH_MOBILE_DIR:-$HOME/work/brulion-perf-mobile}
mkdir -p "$W/logs"
adb() { "$SDK/platform-tools/adb" -s "$SERIAL" "$@"; }

booted() { [[ $(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r') == 1 ]]; }

case ${1:-} in
  create)
    if [[ ! -f $ANDROID_AVD_HOME/$AVD.ini ]]; then
      echo no | sh "$SDK/cmdline-tools/latest/bin/avdmanager" create avd --force -n "$AVD" -k "$IMAGE" -d pixel_6 >/dev/null
    fi
    cfg=$ANDROID_AVD_HOME/$AVD.avd/config.ini
    for kv in hw.ramSize=4096 hw.cpu.ncore=4 disk.dataPartition.size=8G hw.keyboard=yes; do
      sed -i "/^${kv%%=*}[ ]*=/d" "$cfg"
      echo "$kv" >>"$cfg"
    done
    grep -E "^(hw.ramSize|hw.cpu.ncore|hw.lcd|disk.dataPartition|image.sysdir|hw.keyboard)" "$cfg"
    ;;
  start)
    gpu=${2:-swiftshader_indirect}
    if booted; then echo "already running"; exit 0; fi
    emulator=("$SDK/emulator/emulator")
    if [[ -e /etc/NIXOS ]]; then emulator=(steam-run "${emulator[@]}"); fi
    # steam-run's bwrap dies with its parent: keep a shell as that parent for the emulator's life
    # (bash -c with a second command does not exec into the first).
    setsid nohup bash -c '"$@"; echo "emulator exited: $?" >&2' _ "${emulator[@]}" -avd "$AVD" -port "$PORT" \
      -no-window -no-audio -no-boot-anim -no-snapshot ${WIPE:+-wipe-data} -gpu "$gpu" \
      >"$W/logs/emulator-$PORT.log" 2>&1 </dev/null &
    echo $! >"$W/logs/emulator-$PORT.pid"
    timeout 180 "$SDK/platform-tools/adb" -s "$SERIAL" wait-for-device
    for _ in $(seq 1 150); do booted && break; sleep 2; done
    booted || { echo "emu: not booted (see $W/logs/emulator-$PORT.log)"; exit 1; }
    # adbd sometimes refuses the first root request right after boot ("unable to connect for
    # root: closed"): retry.
    for _ in $(seq 1 10); do adb root >/dev/null 2>&1 && break; sleep 3; done
    sleep 3
    timeout 60 "$SDK/platform-tools/adb" -s "$SERIAL" wait-for-device
    for _ in $(seq 1 30); do [[ $(adb shell id -u 2>/dev/null | tr -d '\r') == 0 ]] && break; sleep 1; done
    sleep 2
    adb shell svc power stayon true
    adb shell settings put system screen_off_timeout 1800000
    adb shell settings put global verifier_verify_adb_installs 0 || true
    adb shell wm dismiss-keyguard >/dev/null 2>&1 || true
    adb shell input keyevent 82 >/dev/null 2>&1 || true
    echo "emu: booted $(adb shell getprop ro.build.version.release | tr -d '\r') (API $(adb shell getprop ro.build.version.sdk | tr -d '\r')), gpu $gpu, $(adb shell wm size | tr -d '\r' | sed 's/Physical size: //'), uid $(adb shell id -u | tr -d '\r')"
    ;;
  stop)
    # Flush the guest's page cache first: `emu kill` is a power cut, and files written shortly
    # before (adb push of a vault, the app's saves) come back EMPTY on the next boot (their
    # metadata was journaled, their data never written).
    timeout 60 "$SDK/platform-tools/adb" -s "$SERIAL" shell sync >/dev/null 2>&1 || true
    adb emu kill >/dev/null 2>&1 || true
    sleep 4
    if [[ -f $W/logs/emulator-$PORT.pid ]]; then
      pid=$(<"$W/logs/emulator-$PORT.pid")
      # Only the process this script started (steam-run wraps the emulator binary).
      kill "$pid" 2>/dev/null || true
      rm -f "$W/logs/emulator-$PORT.pid"
    fi
    echo "emu: stopped"
    ;;
  quiet)
    limit=${2:-1.0}
    for _ in $(seq 1 120); do
      load=$(adb shell cat /proc/loadavg 2>/dev/null | cut -d' ' -f1)
      [[ -n $load ]] || { timeout 30 "$SDK/platform-tools/adb" -s "$SERIAL" wait-for-device; sleep 2; continue; }
      awk -v l="$load" -v m="$limit" 'BEGIN { exit !(l < m) }' && { echo "emu: load $load"; exit 0; }
      sleep 5
    done
    echo "emu: still busy (load $load)"
    ;;
  status)
    echo "boot_completed=$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r') load=$(adb shell cat /proc/loadavg 2>/dev/null)"
    adb shell cat /proc/meminfo 2>/dev/null | head -3
    ;;
  adb) shift; adb "$@" ;;
  shell) shift; adb shell "$@" ;;
  *) awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; exit 2 ;;
esac
