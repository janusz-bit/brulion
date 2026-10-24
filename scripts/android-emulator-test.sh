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
# With --keyboard instead: the editing toolbar on the on-screen keyboard (docs/ANDROID.md,
# "Klawiatura ekranowa") — the sample notebook's journal stream and a page, a block edited with a
# finger, the system's keyboard up: the toolbar sits right on its top edge (the IME window's frame
# from dumpsys against the app's KeyboardInsets log and the screenshot), follows its slide frame
# by frame on Android 11+ (the window keeps its size), stays there while the journal scrolls, stays
# docked right above the system's navigation bar when Back hides the keyboard (the shell's log);
# a last screenshot in the dark theme (the system bars' icons). Needs a DEBUGGABLE APK (Qt passes
# app arguments and environment only to those):
#   BRULION_ANDROID_ABIS=x86_64 BRULION_ANDROID_BUILD_TYPE=Debug scripts/android-build.sh
# With --selection instead: selecting and copying blocks with a real finger (docs/UI.md "Selecting
# and copying") — a long press on a block's text in the journal (the selection mode: the keyboard
# stays down, the band, the grips, the selection tools docked at the bottom), a tap extending it,
# Kopiuj ("Skopiowano 2 bloki"), Back.
# Screenshots of each step (sel-*.png); the same debuggable APK.
# With --folders instead: folders, links and tags with a real finger — a tap on a #tag in the
# journal opens its page (not created by looking at it) and Back returns; the editing toolbar's
# "Więcej" opens the block menu, "Kopiuj odnośnik" copies a reference to the block (its id written
# into the note, the copy pasted back into another block with Ctrl+V on Android 13+); the
# drawer's "Strony" header "+" makes a folder (its name typed on the keyboard, "Utwórz" tapped
# while the keyboard still composes the word); a page's "⋯" in the folder tree → "Przenieś do…"
# (the bottom sheet, the folder searched for, what the move rewrites) moves it there, and the
# files say so: the page in its new folder, the query filter naming its old folder rewritten.
# Screenshots of each step (fl-*.png); the same debuggable APK.
# --selection and --folders find what they tap by its accessible name (UiAutomator reads Qt's
# accessibility tree; the keyboard's keys from its dump of every window),
# so they work on other screens and Android versions too (checked: Android 16 and 11, Pixel 6).
#
# Usage: scripts/android-emulator-test.sh [APP_APK SELFTEST_APK]
#        scripts/android-emulator-test.sh --keyboard [DEBUG_APP_APK]
#        scripts/android-emulator-test.sh --selection [DEBUG_APP_APK]
#        scripts/android-emulator-test.sh --folders [DEBUG_APP_APK]
#   default: dist-android/brulion-<version>-x86_64.apk, dist-android/brulion-selftest-<version>-x86_64.apk
# Environment (optional):
#   ANDROID_SDK_ROOT         Android SDK (default ~/.cache/brulion-android-sdk); the emulator and
#                            the system image are installed with its sdkmanager when missing
#   BRULION_EMULATOR_IMAGE   default "system-images;android-36;google_apis;x86_64" (--keyboard,
#                            --selection and --folders also run on older ones, e.g. android-30)
#   BRULION_EMULATOR_PORT    console port (default 5584; adb serial emulator-<port>; 5554-5584:
#                            the emulator warns that adb may not work with others)
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
KEYBOARD=0
SELECTION=0
FOLDERS=0
if [[ ${1:-} == --keyboard ]]; then
  KEYBOARD=1
  shift
elif [[ ${1:-} == --selection ]]; then
  SELECTION=1
  shift
elif [[ ${1:-} == --folders ]]; then
  FOLDERS=1
  shift
fi
APP_APK=${1:-dist-android/brulion-$VERSION-x86_64.apk}
SELFTEST_APK=${2:-dist-android/brulion-selftest-$VERSION-x86_64.apk}
[[ -f $APP_APK ]] || die "$APP_APK not found (build it: BRULION_ANDROID_ABIS=x86_64 BRULION_ANDROID_SELFTEST=1 scripts/android-build.sh)"
((KEYBOARD || SELECTION || FOLDERS)) || [[ -f $SELFTEST_APK ]] || die "$SELFTEST_APK not found"

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
# Software rendering through ANGLE: swiftshader_indirect leaves black stripes on Qt Quick
# buttons, so its screenshots would show a broken UI.
"${emulator[@]}" -avd "$AVD" -port "$PORT" -no-window -no-audio -no-boot-anim -no-snapshot -wipe-data \
  -gpu swangle_indirect >"$OUT/emulator.log" 2>&1 &
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

# --- the editing toolbar on the keyboard (--keyboard) -------------------------------------------

screen_size=$(adb shell wm size | sed -nE 's/.*: ([0-9]+)x([0-9]+).*/\1 \2/p' | tail -n 1)
read -r SCREEN_W SCREEN_H <<<"$screen_size"
DENSITY=$(adb shell wm density | sed -nE 's/.*: ([0-9]+).*/\1/p' | tail -n 1)
ime_shown() { adb shell dumpsys input_method | grep -qE 'mInputShown=true|isInputViewShown=true'; }
ime_hidden() { ! ime_shown; }
# The top of the keyboard on screen (physical px): the visible IME insets source of
# `dumpsys window displays` — "type=ime frame=[l,t][r,b] visible=true" (Android 12+), "type=ITYPE_IME
# frame=… visibleFrame=[l,t][r,b] visible=true" (Android 11).
ime_top() {
  adb shell dumpsys window displays >"$OUT/dumpsys-displays.txt"
  adb shell dumpsys window InputMethod >"$OUT/dumpsys-ime.txt"
  grep -E 'InsetsSource .*type=(ime|ITYPE_IME) .*visible=true' "$OUT/dumpsys-displays.txt" | head -n 1 |
    awk '{ v = ""; f = "";
           if (match($0, /visibleFrame=\[[0-9]+,[0-9]+\]/)) { v = substr($0, RSTART, RLENGTH); gsub(/.*,|\]/, "", v) }
           if (match($0, / frame=\[[0-9]+,[0-9]+\]/)) { f = substr($0, RSTART, RLENGTH); gsub(/.*,|\]/, "", f) }
           print (v != "" && v > 0) ? v : f }'
}
# The app's last settled report: KeyboardInsets (logical px, C++) …
app_overlap() {
  adb logcat -d | grep -oE 'On-screen keyboard (shown|hidden), overlap [0-9]+ px \(platform\)' | tail -n 1 |
    sed -nE 's/.*overlap ([0-9]+) px.*/\1/p'
}
# … and the observer's own (ImeInsets.kt, physical px): "view <top>..<bottom>: overlap <px>".
observer_report() {
  adb logcat -d -s BrulionIme:D | grep 'animating=false' | tail -n 1 |
    sed -nE 's/.*view [0-9]+\.\.([0-9]+): overlap ([0-9]+) .*/\1 \2/p'
}
# What the app shows ends at the keyboard's top: the bottom of Qt's view minus the reported overlap
# is the IME's top (±3 px; a window the system resized above the keyboard reports 0), and C++ got
# the same overlap (±1 dp).
keyboard_matches() {
  local top report overlap
  top=$(ime_top)
  report=$(observer_report)
  overlap=$(app_overlap)
  echo "    IME top ${top:-?} px; Qt's view ends at ${report% *} px, covered ${report#* } px; KeyboardInsets ${overlap:-?} dp"
  [[ -n $top && -n $report && -n $overlap ]] || return 1
  awk -v top="$top" -v r="$report" -v ov="$overlap" -v d="$DENSITY" 'BEGIN {
    split(r, a, " "); bottom = a[1]; px = a[2]
    gap = bottom - px - top; if (gap < 0) gap = -gap
    dp = px * 160 / d - ov; if (dp < 0) dp = -dp
    exit !(gap <= 3 && dp <= 1) }'
}
keyboard_reported_hidden() { adb logcat -d | grep 'On-screen keyboard' | tail -n 1 | grep -q 'hidden, overlap 0'; }
# The shell's last settled layout while typing (AppShell.qml, logical px): "Typing: the content
# ends <room> px above the window's bottom (keyboard <k> px, navigation bar <s> px), editing
# toolbar docked". With the keyboard hidden by the system the toolbar stays docked right above the
# navigation bar: the content ends exactly that far above the bottom.
typing_report() {
  adb logcat -d | grep -oE "Typing: the content ends [0-9]+ px above the window's bottom \(keyboard [0-9]+ px, navigation bar [0-9]+ px\), editing toolbar (docked|not docked)" |
    tail -n 1
}
toolbar_above_navigation_bar() {
  local report
  report=$(typing_report)
  [[ $report =~ ends\ ([0-9]+)\ px.*keyboard\ 0\ px,\ navigation\ bar\ ([0-9]+)\ px.*toolbar\ docked$ ]] &&
    ((BASH_REMATCH[1] == BASH_REMATCH[2]))
}
app_shot() { adb exec-out screencap -p >"$OUT/$1.png" || true; }
start_app() { # <app arguments> [more am start arguments]
  adb shell am force-stop "$PKG"
  adb logcat -c
  adb shell am start -W -n "$ACTIVITY" --es applicationArguments "'$1'" \
    --es extraenvvars_QT_LOGGING_RULES "brulion.platform.debug=true" "${@:2}" >/dev/null
  sleep 25
}
# Reports of the keyboard observer (ImeInsets.kt logs each one with this property set).
ime_frames() { adb logcat -d -s BrulionIme:D | grep -c 'animating=true' || true; }
android_api=$(adb shell getprop ro.build.version.sdk | tr -d '\r')

if ((KEYBOARD)); then
  info "Keyboard: $APP_APK on ${SCREEN_W}x$SCREEN_H, density $DENSITY, API $android_api"
  adb install -r -g "$APP_APK" >/dev/null
  adb shell setprop log.tag.BrulionIme DEBUG
  adb shell run-as "$PKG" true 2>/dev/null || die "$APP_APK is not debuggable (BRULION_ANDROID_BUILD_TYPE=Debug)"
  # The journal stream: today's first block tapped with a finger (the sample notebook on a
  # Pixel 6: "Pierwszy dzień z Brulionem" a quarter down the screen).
  start_app '--demo --locale pl --route journals'
  check "the app is running" running
  app_shot keyboard-journal-start
  adb shell input tap $((SCREEN_W * 24 / 100)) $((SCREEN_H * 25 / 100))
  check "a tap on today's block brings up the keyboard" wait_for 15 ime_shown
  sleep 3
  app_shot keyboard-journal
  check "the app reports the keyboard (KeyboardInsets, platform)" wait_for 10 log_has 'On-screen keyboard shown, overlap'
  check "the app's view ends at the keyboard's top" keyboard_matches
  frames=$(ime_frames)
  report=$(observer_report)
  if ((android_api >= 30)); then
    # (Android 11+: the window is drawn behind the system bars and keeps its size, the keyboard
    # covers its bottom; keyboardsources.cpp.)
    check "the window keeps its size: the keyboard covers its bottom (Android 11+)" test "${report#* }" -gt 0
    check "the keyboard's slide is followed frame by frame (Android 11+, $frames frames)" test "$frames" -ge 3
  else
    echo "    the system resized the window above the keyboard ($frames frame(s) of a slide reported)"
  fi
  # The journal scrolls (a finger on the days above the keyboard): the toolbar stays on it.
  adb shell input swipe $((SCREEN_W / 2)) $((SCREEN_H * 35 / 100)) $((SCREEN_W / 2)) $((SCREEN_H * 15 / 100)) 400
  sleep 2
  app_shot keyboard-journal-scrolled
  check "scrolling the journal leaves the keyboard up" ime_shown
  # Back hides the keyboard; the toolbar stays docked at the bottom while editing goes on.
  adb shell input keyevent 4
  check "Back hides the keyboard" wait_for 10 ime_hidden
  sleep 2
  app_shot keyboard-journal-hidden
  check "the app reports it hidden" wait_for 10 keyboard_reported_hidden
  check "the toolbar stays docked right above the system's navigation bar, editing goes on" \
    wait_for 10 toolbar_above_navigation_bar
  echo "    $(typing_report)"
  # A page: the guide (opened as by a tapped notification), its first paragraph tapped.
  start_app '--demo --locale pl' --es "$EXTRA.PATH" "'Witaj w Brulionie.md'" --ei "$EXTRA.LINE" 0
  app_shot keyboard-page-start
  # Its first paragraphs lie 25–45 % down the screen (the system bars differ between versions).
  for fraction in 40 30 35 45 27; do
    adb shell input tap $((SCREEN_W * 30 / 100)) $((SCREEN_H * fraction / 100))
    wait_for 6 ime_shown && break
  done
  check "a tap on the guide's text brings up the keyboard" wait_for 10 ime_shown
  sleep 3
  app_shot keyboard-page
  check "the page's view ends at the keyboard's top" keyboard_matches
  # The window is drawn behind the system bars (Android 11+): their icons follow the app's theme
  # (the screenshots: dark icons over the light theme above, light ones over the dark theme here).
  start_app '--demo --locale pl --theme dark --route journals'
  app_shot keyboard-dark-theme
  adb logcat -d >"$OUT/keyboard-logcat.txt"
  check "no crash" bash -c "! grep -E 'FATAL EXCEPTION|Fatal signal' '$OUT/keyboard-logcat.txt'"
  grep -E 'On-screen keyboard|BrulionIme|brulion.platform' "$OUT/keyboard-logcat.txt" >"$OUT/keyboard-summary.txt" || true
  info "Result"
  cat "$OUT/keyboard-summary.txt"
  echo "logs and screenshots: $(cd "$OUT" && pwd)"
  ((failures == 0)) || die "$failures check(s) failed"
  echo "all checks passed"
  exit 0
fi

# --- items on screen found by their names (--selection, --folders) -----------------------------

# Qt for Android gives its accessibility tree to UiAutomator (its service turns accessibility on):
# items are found on screen by their accessible names (content-desc: a button's label and its
# shortcut, a block's text, a toast with a button; plain labels have none).
ui_dump() {
  adb shell rm -f /sdcard/brulion-ui.xml
  adb shell uiautomator dump /sdcard/brulion-ui.xml >/dev/null 2>&1 || true
  adb exec-out cat /sdcard/brulion-ui.xml >"$OUT/ui.xml" 2>/dev/null || true
}
# The centres "x y" of the nodes whose text or content-desc is the whole ERE, top to bottom (a new
# dump). Only those on the screen: a row scrolled away or hidden keeps its node (bounds 0,0,0,0).
ui_find_all() {
  ui_dump
  sed 's/<node /\n<node /g' "$OUT/ui.xml" | grep -E "(text|content-desc)=\"($1)\"" |
    sed -nE 's/.*bounds="\[([0-9]+),([0-9]+)\]\[([0-9]+),([0-9]+)\]".*/\1 \2 \3 \4/p' |
    awk -v w="$SCREEN_W" -v h="$SCREEN_H" '$3 > $1 && $4 > $2 && $3 <= w && $4 <= h {
      print int(($1 + $3) / 2), int(($2 + $4) / 2) }' | sort -n -k 2
}
ui_has() { [[ -n $(ui_find_all "$1") ]]; }
ui_lacks() { ! ui_has "$1"; }
ui_y() { ui_find_all "$1" | head -n 1 | cut -d ' ' -f 2; } # <ERE>: the topmost one's centre
ui_tap() { # <ERE> [first|last] [pause]: a tap on the item named so (the topmost or the bottommost;
  # a dump taken while something moves can fail: tried again), then a pause (2 s)
  local xy="" _
  for _ in 1 2 3 4 5; do
    if [[ ${2:-first} == last ]]; then xy=$(ui_find_all "$1" | tail -n 1); else xy=$(ui_find_all "$1" | head -n 1); fi
    [[ -n $xy ]] && break
    sleep 2
  done
  if [[ -z $xy ]]; then
    echo "    not on screen: $1"
    return 1
  fi
  adb shell input tap "$xy"
  sleep "${3:-2}"
}
# A word typed with a finger on the on-screen keyboard: its keys found by their names in
# UiAutomator's dump of every window (`--windows`: the keyboard's too); letters and spaces, the
# case as the keyboard has it (it capitalizes a field's first letter itself). Not `input text`:
# Gboard on Android 11 composed the injected keys after the first ones and the word came out
# shuffled ("Archiwum" -> "rchiwumA"). Without a dump of the keyboard: `input text` after all.
type_on_keyboard() { # <word>
  local keys i ch key
  adb shell rm -f /sdcard/brulion-ime.xml
  adb shell uiautomator dump --windows /sdcard/brulion-ime.xml >/dev/null 2>&1 || true
  keys=$(adb exec-out cat /sdcard/brulion-ime.xml 2>/dev/null | sed 's/<node /\n<node /g' | grep 'inputmethod' |
    sed -nE 's/.*content-desc="([A-Za-z]|Space)".*bounds="\[([0-9]+),([0-9]+)\]\[([0-9]+),([0-9]+)\]".*/\1 \2 \3 \4 \5/p' || true)
  if [[ -z $keys ]]; then
    echo "    (the keyboard's keys are not in the dump: \`input text\`)"
    adb shell input text "${1// /%s}"
    return
  fi
  for ((i = 0; i < ${#1}; i++)); do
    ch=${1:i:1}
    [[ $ch == " " ]] && ch=Space
    key=$(awk -v c="$ch" 'tolower($1) == tolower(c) { print int(($2 + $4) / 2), int(($3 + $5) / 2); exit }' <<<"$keys")
    if [[ -z $key ]]; then
      echo "    no key \"$ch\" on the keyboard"
      return 1
    fi
    adb shell input tap "$key"
    sleep 0.3
  done
}
back_to() { # <ERE>: Back until the item shows (the keyboard first, then the history)
  local _
  for _ in 1 2 3; do
    ui_has "$1" && return 0
    adb shell input keyevent 4
    sleep 2
  done
  ui_has "$1"
}
# The sample notebook's files (the app's private storage: run-as on a debuggable APK).
VAULT="files/Przykładowy brulion"
vault_cat() { adb shell run-as "$PKG" cat "\"$VAULT/$1\"" 2>/dev/null | tr -d '\r'; }
vault_has() { adb shell run-as "$PKG" ls -d "\"$VAULT/$1\"" >/dev/null 2>&1; }
vault_lacks() { ! vault_has "$1"; }
vault_grep() { vault_cat "$1" | grep -qE -- "$2"; } # <file> <ERE>
# The notebook's top-level entry named so in any letter case (a keyboard capitalizes a field's
# first letter or not).
vault_name() { adb shell run-as "$PKG" ls "\"$VAULT\"" 2>/dev/null | tr -d '\r' | grep -ix -- "$1" | head -n 1; }

# --- selecting and copying with a finger (--selection) -------------------------------------------

long_press() { adb shell input swipe "$1" "$2" "$1" "$2" 1400; } # <x> <y>

if ((SELECTION)); then
  info "Selection: $APP_APK on ${SCREEN_W}x$SCREEN_H, density $DENSITY, API $android_api"
  adb install -r -g "$APP_APK" >/dev/null
  adb shell run-as "$PKG" true 2>/dev/null || die "$APP_APK is not debuggable (BRULION_ANDROID_BUILD_TYPE=Debug)"
  first_block='Pierwszy dzień z Brulionem #przewodnik'
  start_app '--demo --locale pl --route journals'
  check "the app is running" running
  check "UiAutomator sees the app's items (accessibility)" wait_for 30 ui_has "$first_block"
  app_shot sel-1-journal
  # Today's first block, found by its text.
  long_press $((SCREEN_W * 24 / 100)) "$(ui_y "$first_block")"
  sleep 2
  app_shot sel-2-long-press
  check "a long press on a block's text brings up no keyboard (the selection mode)" ime_hidden
  check "the selection's tools are docked" ui_has 'Kopiuj, Ctrl\+C'
  # The next block: a tap extends the selection.
  adb shell input tap $((SCREEN_W * 30 / 100)) "$(ui_y 'Przejrzeć Remont mieszkania.*')"
  sleep 2
  app_shot sel-3-extended
  check "a tap in the selection mode edits nothing (no keyboard)" ime_hidden
  # Kopiuj in the docked tools, right above the navigation bar.
  check "Kopiuj copies the selection" ui_tap 'Kopiuj, Ctrl\+C' last 0
  check "the toast: two blocks copied" wait_for 6 ui_has 'Skopiowano 2 bloki'
  app_shot sel-4-copied
  adb shell input keyevent 4
  sleep 2
  app_shot sel-5-back
  check "Back ends the selection mode" ui_lacks 'Kopiuj, Ctrl\+C'
  check "the app is still running" running
  adb logcat -d >"$OUT/selection-logcat.txt"
  check "no crash" bash -c "! grep -E 'FATAL EXCEPTION|Fatal signal' '$OUT/selection-logcat.txt'"
  check "no QML errors" bash -c "! grep -E 'TypeError|ReferenceError|Binding loop detected' '$OUT/selection-logcat.txt'"
  grep -E 'qml|Skopiowano|brulion' "$OUT/selection-logcat.txt" | tail -n 40 >"$OUT/selection-summary.txt" || true
  info "Result"
  cat "$OUT/selection-summary.txt"
  echo "logs and screenshots: $(cd "$OUT" && pwd)"
  ((failures == 0)) || die "$failures check(s) failed"
  echo "all checks passed"
  exit 0
fi

# --- folders, links and tags with a finger (--folders) -------------------------------------------

if ((FOLDERS)); then
  info "Folders, links and tags: $APP_APK on ${SCREEN_W}x$SCREEN_H, density $DENSITY, API $android_api"
  adb install -r -g "$APP_APK" >/dev/null
  adb shell run-as "$PKG" true 2>/dev/null || die "$APP_APK is not debuggable (BRULION_ANDROID_BUILD_TYPE=Debug)"
  today_journal="journals/$(adb shell date +%Y-%m-%d | tr -d '\r').md"
  first_block='Pierwszy dzień z Brulionem #przewodnik'
  start_app '--demo --locale pl --route journals'
  check "the app is running" running
  check "UiAutomator sees the app's items (accessibility)" wait_for 30 ui_has "$first_block"
  app_shot fl-1-journal

  # A tag is a link to the page it names. "#przewodnik" ends today's first block (three quarters
  # across on a Pixel 6): the welcome page has the alias "Przewodnik" — it opens with the tag's
  # strip. "#pomysł" ends the fifth: no page has that name — it opens empty, not created.
  adb shell input tap $((SCREEN_W * 76 / 100)) "$(ui_y "$first_block")"
  sleep 3
  app_shot fl-2-tag-alias
  check "a tap on #przewodnik opens the page with that alias" ui_has 'Witaj w Brulionie'
  check "with the tag's strip (Widok tagu)" ui_has 'Widok tagu.*'
  check "Back returns to the journal" back_to "$first_block"
  adb shell input tap $((SCREEN_W * 79 / 100)) "$(ui_y 'Pomysł na obiad: .*')"
  sleep 3
  app_shot fl-3-tag-page
  check "a tap on #pomysł opens its page" ui_has 'pomysł'
  check "the blocks tagged #pomysł are among its references" ui_has 'Pomysł na obiad: .*, Otwiera blok.*'
  check "the page is not created by looking at it" vault_lacks pomysł.md
  check "Back returns to the journal" back_to "$first_block"

  # The editing toolbar's "Więcej": the block menu of the block being edited; "Kopiuj odnośnik" →
  # "Odwołanie do bloku" writes the block's id into the note and copies ((id)).
  adb shell input tap $((SCREEN_W * 24 / 100)) "$(ui_y "$first_block")"
  check "a tap on today's first block brings up the keyboard" wait_for 15 ime_shown
  sleep 2
  # The toolbar scrolls sideways: "Więcej" is near its end (a finger swipes the row there).
  bar_y=$(ui_y 'Narzędzia edycji')
  [[ -n $bar_y ]] && adb shell input swipe $((SCREEN_W * 85 / 100)) "$bar_y" $((SCREEN_W * 10 / 100)) "$bar_y" 300
  sleep 2
  app_shot fl-4-toolbar-end
  # (The top bar has a "Więcej" too: the toolbar's is the lower one.)
  check "the toolbar's Więcej opens the block menu" ui_tap 'Więcej' last
  app_shot fl-5-block-menu
  check "the block menu offers Kopiuj odnośnik" ui_tap 'Kopiuj odnośnik'
  app_shot fl-6-copy-link-menu
  # (No pause: its toast goes after a few seconds.)
  check "Odwołanie do bloku copies the reference" ui_tap 'Odwołanie do bloku.*' first 0
  check "the toast quotes the copied ((id))" wait_for 6 ui_has 'Skopiowano \(\([A-Za-z0-9_-]+\)\)'
  app_shot fl-7-link-copied
  block_id=$(vault_cat "$today_journal" | sed -nE '1s/.* \^([A-Za-z0-9_-]+)[[:space:]]*$/\1/p')
  echo "    the first block's id in $today_journal: ${block_id:-none}"
  check "the block got its id in the note" test -n "$block_id"
  # The clipboard read back through the app: pasted at the end of another block with Ctrl+V
  # (`input keycombination`, Android 13+), the reference lands in the note.
  if ((android_api >= 33)); then
    adb shell input tap $((SCREEN_W * 25 / 100)) "$(ui_y 'Przeczytane dziś: .*')"
    sleep 2
    adb shell input keycombination 113 123 # Ctrl+End
    adb shell input keyevent 62            # a space
    adb shell input keycombination 113 50  # Ctrl+V
    sleep 2
    app_shot fl-8-pasted
    check "the copied ((id)) pastes back into another block" \
      wait_for 10 vault_grep "$today_journal" "^- Przeczytane dziś: .*\[\[Książki 2026\]\] \(\($block_id\)\)$"
  else
    echo "    (no paste: \`input keycombination\` needs Android 13+)"
  fi

  # A new folder from the drawer's "Strony" header: its "+" → "Nowy folder", the name typed.
  ui_tap 'Menu' first
  check "the drawer shows the Strony section" wait_for 10 ui_has 'Strony.*'
  app_shot fl-9-drawer
  check "the Strony header's + opens its menu" ui_tap 'Nowa strona lub folder'
  check "its Nowy folder opens the dialog" ui_tap 'Nowy folder'
  # The name typed on the keyboard, "Utwórz" tapped while it still composes the word.
  type_on_keyboard Archiwum
  sleep 1
  app_shot fl-10-new-folder
  check "Utwórz takes the word the keyboard still composes" ui_tap 'Utwórz' last
  check "the folder Archiwum is made" wait_for 10 vault_name Archiwum
  folder=$(vault_name Archiwum || true)
  folder=${folder:-Archiwum}
  echo "    the folder: $folder"
  check "and shows in the tree" ui_has "$folder(, folder .*)?"
  app_shot fl-11-folder-made

  # "⋯" on a page of the tree → "Przenieś do…": "Remont mieszkania" from Projekty into Archiwum.
  ui_has 'Więcej: Remont mieszkania' || ui_tap 'Projekty, folder .*'
  check "the page's row has its ⋯ button" ui_tap 'Więcej: Remont mieszkania'
  app_shot fl-12-row-menu
  check "its menu offers Przenieś do…" ui_tap 'Przenieś do…'
  check "the move sheet opens" ui_has 'Przenieś „Remont mieszkania”'
  type_on_keyboard Archiwum # its search field has the keyboard
  sleep 1
  adb shell input keyevent 66 # Enter: the first folder found
  sleep 2
  # (Its "Zaktualizowane linki: 1 (w 1 notatce)" is plain text: the screenshot shows it.)
  app_shot fl-13-move-sheet
  check "Przenieś tutaj moves it" ui_tap 'Przenieś tutaj' last
  moved_toast='Przeniesiono do „'"$folder"'”, zaktualizowano 1 link'
  check "the toast says what the move rewrote, with Cofnij" wait_for 6 ui_has "$moved_toast"
  app_shot fl-14-moved
  check "the page is in $folder" vault_has "$folder/Remont mieszkania.md"
  check "and no longer in Projekty" vault_lacks 'Projekty/Remont mieszkania.md'
  check "its query's filter path:Projekty follows it (path:$folder)" \
    vault_grep "$folder/Remont mieszkania.md" "^type:tasks path:$folder status:open sort:due\$"
  check "the journal's links to it are unchanged" vault_grep "$today_journal" '\[\[Remont mieszkania\]\]'

  adb logcat -d >"$OUT/folders-logcat.txt"
  check "no crash" bash -c "! grep -E 'FATAL EXCEPTION|Fatal signal' '$OUT/folders-logcat.txt'"
  check "no QML errors" bash -c "! grep -E 'TypeError|ReferenceError|Binding loop detected' '$OUT/folders-logcat.txt'"
  grep -E 'qml|brulion' "$OUT/folders-logcat.txt" | tail -n 40 >"$OUT/folders-summary.txt" || true
  info "Result"
  cat "$OUT/folders-summary.txt"
  echo "logs and screenshots: $(cd "$OUT" && pwd)"
  ((failures == 0)) || die "$failures check(s) failed"
  echo "all checks passed"
  exit 0
fi

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
