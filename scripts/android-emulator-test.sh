#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# End-to-end test of Brulion's Android part on an emulator (x86_64, Android 16 / API 36):
#   1. the app APK starts and keeps running (no crash), notification channels exist;
#   2. the bridge self-test APK (src/platform/android/selftest): JNI calls work, reminders are
#      armed in AlarmManager (the past one skipped), the reminder and the alarm notification
#      fire on their channels, the alarm turns into "Przegapiony alarm" after its ring time,
#      notification taps reach C++ (cold start and while running, Polish path).
# Needs an x86_64 Linux host with KVM. Build both APKs first:
#   BRULION_ANDROID_ABIS=x86_64 BRULION_ANDROID_SELFTEST=1 scripts/android-build.sh
#
# Usage: scripts/android-emulator-test.sh [APP_APK SELFTEST_APK]
#   default: dist-android/brulion-<version>-x86_64.apk, dist-android/brulion-selftest-<version>-x86_64.apk
# Environment (optional):
#   ANDROID_SDK_ROOT         Android SDK (default ~/.cache/brulion-android-sdk); the emulator and
#                            the system image are installed with its sdkmanager when missing
#   BRULION_EMULATOR_IMAGE   default "system-images;android-36;google_apis;x86_64"
#   BRULION_EMULATOR_PORT    console port (default 5584; adb serial emulator-<port>)
#   BRULION_EMULATOR_OUT     logs and screenshots (default dist-android/emulator-test)
# On NixOS the emulator runs under steam-run (its bundled libraries expect an FHS system).
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
SCRIPT="$ROOT/scripts/$(basename -- "${BASH_SOURCE[0]}")"
cd "$ROOT"

info() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
die() {
  printf '\033[1;31merror:\033[0m %s\n' "$*" >&2
  exit 1
}

case ${1:-} in -h | --help)
  awk 'NR > 2 && /^#/ { sub(/^# ?/, ""); print; next } NR > 2 { exit }' "$SCRIPT"
  exit 0
  ;;
esac

[[ -e /dev/kvm ]] || die "no /dev/kvm: the x86_64 emulator needs KVM"

if [[ -e /etc/NIXOS && -z ${BRULION_ANDROID_IN_NIX_SHELL:-} ]]; then
  export BRULION_ANDROID_IN_NIX_SHELL=1
  exec nix shell nixpkgs#jdk21_headless --command bash "$SCRIPT" "$@"
fi

VERSION=$(sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt | head -n 1)
APP_APK=${1:-dist-android/brulion-$VERSION-x86_64.apk}
SELFTEST_APK=${2:-dist-android/brulion-selftest-$VERSION-x86_64.apk}
[[ -f $APP_APK ]] || die "$APP_APK not found (build it: BRULION_ANDROID_ABIS=x86_64 BRULION_ANDROID_SELFTEST=1 scripts/android-build.sh)"
[[ -f $SELFTEST_APK ]] || die "$SELFTEST_APK not found"

SDK=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/.cache/brulion-android-sdk}}
IMAGE=${BRULION_EMULATOR_IMAGE:-system-images;android-36;google_apis;x86_64}
PORT=${BRULION_EMULATOR_PORT:-5584}
SERIAL=emulator-$PORT
OUT=${BRULION_EMULATOR_OUT:-dist-android/emulator-test}
PKG=io.github.januszbit.brulion
ACTIVITY=$PKG/$PKG.android.BrulionActivity
EXTRA=$PKG.android.extra
export ANDROID_SDK_ROOT=$SDK ANDROID_HOME=$SDK
export ANDROID_USER_HOME=${ANDROID_USER_HOME:-$HOME/.cache/brulion-android-user}
export ANDROID_AVD_HOME=$ANDROID_USER_HOME/avd
mkdir -p "$OUT" "$ANDROID_AVD_HOME"
rm -f -- "$OUT"/*

if [[ -z ${JAVA_HOME:-} ]]; then
  JAVA_HOME=$(dirname -- "$(dirname -- "$(readlink -f -- "$(command -v java)")")")
  export JAVA_HOME
fi
sdkmanager=$SDK/cmdline-tools/latest/bin/sdkmanager
avdmanager=$SDK/cmdline-tools/latest/bin/avdmanager
image_dir=$SDK/$(tr ';' '/' <<<"$IMAGE")
packages=()
[[ -x $SDK/emulator/emulator ]] || packages+=(emulator)
[[ -x $SDK/platform-tools/adb ]] || packages+=(platform-tools)
[[ -d $image_dir ]] || packages+=("$IMAGE")
if ((${#packages[@]})); then
  info "Installing ${packages[*]}"
  yes 2>/dev/null | sh "$sdkmanager" --sdk_root="$SDK" --licenses >/dev/null || true
  sh "$sdkmanager" --sdk_root="$SDK" --install "${packages[@]}" | grep -v '^\[' || true
fi
[[ -d $image_dir ]] || die "system image $IMAGE was not installed"

AVD=brulion-test-$(tr ';' '-' <<<"$IMAGE" | sed 's/^system-images-//')
if [[ ! -f $ANDROID_AVD_HOME/$AVD.ini ]]; then
  info "Creating the AVD $AVD"
  echo no | sh "$avdmanager" create avd --force -n "$AVD" -k "$IMAGE" -d pixel_6 >/dev/null
fi

adb() { "$SDK/platform-tools/adb" -s "$SERIAL" "$@"; }
emulator=("$SDK/emulator/emulator")
if [[ -e /etc/NIXOS ]] && command -v steam-run >/dev/null; then emulator=(steam-run "${emulator[@]}"); fi

info "Starting the emulator ($AVD, $SERIAL)"
"${emulator[@]}" -avd "$AVD" -port "$PORT" -no-window -no-audio -no-boot-anim -no-snapshot -wipe-data \
  -gpu swiftshader_indirect >"$OUT/emulator.log" 2>&1 &
emulator_pid=$!
cleanup() {
  adb emu kill >/dev/null 2>&1 || true
  sleep 3
  kill "$emulator_pid" 2>/dev/null || true
}
trap cleanup EXIT

timeout 120 "$SDK/platform-tools/adb" -s "$SERIAL" wait-for-device || die "the emulator did not come up (see $OUT/emulator.log)"
for _ in $(seq 1 120); do
  [[ $(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r') == 1 ]] && break
  sleep 2
done
[[ $(adb shell getprop sys.boot_completed | tr -d '\r') == 1 ]] || die "the emulator did not finish booting"
adb shell input keyevent 82 >/dev/null 2>&1 || true # unlock
echo "booted: $(adb shell getprop ro.build.version.release | tr -d '\r') (API $(adb shell getprop ro.build.version.sdk | tr -d '\r'))"

failures=0
check() { # <description> <command...>
  if "${@:2}"; then
    printf '  \033[32mPASS\033[0m %s\n' "$1"
  else
    printf '  \033[31mFAIL\033[0m %s\n' "$1"
    failures=$((failures + 1))
  fi
}
log_has() { adb logcat -d | grep -q -- "$1"; }
running() { [[ -n $(adb shell pidof "$PKG" | tr -d '\r') ]]; }
notification_on() { # <id> <channel>
  adb shell dumpsys notification --noredact | grep -E "pkg=$PKG .*id=$1 " -A 12 | grep -q "channel=$2"
}
channels_ok() { # Brulion's notification channels exist (its AppSettings section of the dump)
  adb shell dumpsys notification >"$OUT/dumpsys-notification-app.txt"
  local section
  section=$(awk -v key="AppSettings: $PKG " 'index($0, key) { on = 1; next } /AppSettings: / { on = 0 } on' \
    "$OUT/dumpsys-notification-app.txt")
  [[ -n $section ]] || section=$(<"$OUT/dumpsys-notification-app.txt")
  local channel
  for channel in reminders alarms missed-alarms; do
    grep -q "mId='$channel'" <<<"$section" || return 1
  done
}
wait_for() { # <seconds> <command...>
  local deadline=$((SECONDS + $1))
  while ((SECONDS < deadline)); do
    "${@:2}" && return 0
    sleep 3
  done
  return 1
}

# --- 1. the app --------------------------------------------------------------------------------

info "App: $APP_APK"
adb install -r -g "$APP_APK" >/dev/null
adb logcat -c
adb shell am start -W -n "$ACTIVITY" >/dev/null
sleep 15
adb exec-out screencap -p >"$OUT/app.png" || true
adb logcat -d >"$OUT/app-logcat.txt"
check "app is running 15 s after start" running
check "no crash (FATAL EXCEPTION / Fatal signal)" bash -c "! grep -E 'FATAL EXCEPTION|Fatal signal' '$OUT/app-logcat.txt'"
check "notification channels reminders, alarms, missed-alarms" channels_ok
adb shell am force-stop "$PKG"

# --- 2. the bridge self-test --------------------------------------------------------------------

info "Bridge self-test: $SELFTEST_APK"
adb uninstall "$PKG" >/dev/null # same package, other versionCode: a clean install
adb install -g "$SELFTEST_APK" >/dev/null
adb shell pm grant "$PKG" android.permission.POST_NOTIFICATIONS 2>/dev/null || true
adb logcat -c
# Cold start from a "notification tap": the activation must reach C++ via takeActivation.
adb shell am start -W -n "$ACTIVITY" --es "$EXTRA.PATH" dziennik/zimny-start.md --ei "$EXTRA.LINE" 5 >/dev/null
started=$SECONDS
check "scheduleReminders round 1: 2 armed (past one skipped)" wait_for 20 log_has "SELFTEST schedule round=1 ok=1 scheduled=2 exact=1"
check "scheduleReminders round 2: idempotent" log_has "SELFTEST schedule round=2 ok=1 scheduled=2 exact=1"
check "permissions reported (notifications granted, exact alarms)" log_has "SELFTEST permissions(start) allFiles=0 notifications=1 exact=1"
check "cold-start tap delivered to C++" wait_for 10 log_has "SELFTEST activated path=dziennik/zimny-start.md line=5"
# A tap while the app runs: onNewIntent -> nativeActivated (Polish path).
adb shell am start -n "$ACTIVITY" --es "$EXTRA.PATH" "'strony/Łódź.md'" --ei "$EXTRA.LINE" 7 >/dev/null
check "tap while running delivered to C++ (Polish path)" wait_for 10 log_has "SELFTEST activated path=strony/Łódź.md line=7"
adb shell dumpsys alarm >"$OUT/dumpsys-alarm.txt"
check "AlarmManager holds Brulion's alarms" bash -c "grep -q '$PKG' '$OUT/dumpsys-alarm.txt'"
adb shell input keyevent 3 # home: the notifications are posted while the app is in the background
check "reminder 101 fired on channel 'reminders'" wait_for $((60 - (SECONDS - started))) notification_on 101 reminders
check "alarm 102 rings on channel 'alarms'" wait_for $((95 - (SECONDS - started))) notification_on 102 alarms
adb exec-out screencap -p >"$OUT/alarm.png" || true
check "alarm 102 became 'missed-alarms' after its ring time (1 min)" wait_for $((170 - (SECONDS - started))) notification_on 102 missed-alarms
adb shell dumpsys notification --noredact >"$OUT/dumpsys-notification.txt"
adb logcat -d >"$OUT/selftest-logcat.txt"
grep -E 'SELFTEST|BrulionAndroid:|brulion\.android:|AndroidRuntime:' "$OUT/selftest-logcat.txt" >"$OUT/selftest-summary.txt" || true
check "no crash in the self-test" bash -c "! grep -E 'FATAL EXCEPTION|Fatal signal' '$OUT/selftest-logcat.txt'"

info "Result"
cat "$OUT/selftest-summary.txt"
echo "logs and screenshots: $ROOT/$OUT"
((failures == 0)) || die "$failures check(s) failed"
echo "all checks passed"
