#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# End-to-end test of Brulion's Android part on an emulator (x86_64, Android 16 / API 36):
#   1. the app APK starts and keeps running (no crash), notification channels exist;
#   2. the bridge self-test APK (src/platform/android/selftest): JNI calls work, reminders are
#      armed in AlarmManager (the past one skipped), the reminder and the alarm notification
#      fire on their channels, the alarm turns into "Przegapiony alarm" after its ring time,
#      notification taps reach C++ (cold start and while running, Polish path), "Zrobione" in a
#      task reminder's notification removes it and reaches C++ with the day of the tap — at once
#      with the app on screen, at the next start when the app's process was gone (queued).
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
# With --share instead: "Udostępnij → Brulion" (docs/ANDROID.md) — text shared to ShareActivity
# while the app runs (the share dialog, "Zapisz": a block of today's journal, no date parsing), a
# .md file on shared storage shared through MediaStore after a cold start (a new page, its bytes
# kept, CRLF too), a picture refused (no dialog: ShareActivity accepts text/* only), several files
# selected in the system's file manager and shared (two text files and a picture: the picture
# listed as not a text file, or left out when the share sheet does not offer Brulion for it; a
# block per text file in today's journal), and the copies in files/share-in removed. Screenshots of each step (sh-*.png); the same debuggable APK.
# With --widgets instead: the home-screen widgets (docs/ANDROID.md "Widgety") — the sample
# notebook publishes their snapshot when the app leaves the front; the three widgets are placed
# through the launcher's widget picker (tap a preview, "Add"); quick add with the app killed writes
# into today's journal at once (Qt not even loaded), a task for today shows in the tasks widget,
# a time and "Alarm" write "⏰!" and arm a provisional native reminder, an entry without a
# snapshot or with the notebook's folder gone (never made again) waits in the inbox and the app
# writes it at its next start, with the app alive in the background the provisional reminder
# gives way once the app is back in front, a tap on a widget's task
# opens the note; the calendar's month buttons; screenshots of each widget light and dark, the
# tasks widget made taller (tomorrow's tasks) and the calendar lower (the agenda only) with the
# launcher's resize handles (wg-*.png). Needs a google_apis image (adb root: quick add and the
# widgets' receiver are not exported) and the same debuggable APK.
# With --web-pictures instead: pictures from the internet (docs/ANDROID.md "Obrazy z internetu") — a
# notebook whose journal has a picture over https (a public site, BRULION_WEBPICTURE_HTTPS) and one
# over http from a server this script starts on the host (the emulator reaches it at 10.0.2.2;
# cleartextTrafficPermitted is false): "Pokaż" fetches each into the cache; after a restart in
# airplane mode both show from the cache; a tap opens the viewer (actual size and back, Back closes
# it), a long press the menu (Powiększ, Zapisz w notatniku, Otwórz w przeglądarce, Kopiuj adres
# obrazu); with the cache cleared and no network "Pokaż" says why ("Brak połączenia z …") and
# "Spróbuj ponownie" works once the network is back; "Zapisz w notatniku" writes the picture into
# assets/ and the note's link to it; "Otwórz w przeglądarce" hands the address to a browser.
# Screenshots of each step (wp-*.png); the same debuggable APK.
# With --done instead: "Zrobione" in a task reminder's notification of the app itself (docs/ANDROID.md
# "Przypomnienia") — a notebook whose journal has two tasks with reminders a few minutes ahead: the
# first one's "Zrobione" tapped with the app on screen marks the task done in the note at once
# (`[x]` and `✅ <the day of the tap>`); the second one's tapped with the app's process gone leaves
# the note as it is until the next start, which writes it. Screenshots (dn-*.png); the same
# debuggable APK.
# --selection, --folders, --share, --widgets, --web-pictures and --done find what they tap by its accessible name (UiAutomator reads Qt's
# accessibility tree; the keyboard's keys from its dump of every window),
# so they work on other screens and Android versions too (checked: Android 16 and 11, Pixel 6).
#
# Usage: scripts/android-emulator-test.sh [APP_APK SELFTEST_APK]
#        scripts/android-emulator-test.sh --keyboard [DEBUG_APP_APK]
#        scripts/android-emulator-test.sh --selection [DEBUG_APP_APK]
#        scripts/android-emulator-test.sh --folders [DEBUG_APP_APK]
#        scripts/android-emulator-test.sh --share [DEBUG_APP_APK]
#        scripts/android-emulator-test.sh --widgets [DEBUG_APP_APK]
#        scripts/android-emulator-test.sh --web-pictures [DEBUG_APP_APK]
#        scripts/android-emulator-test.sh --done [DEBUG_APP_APK]
#   default: dist-android/brulion-<version>-x86_64.apk, dist-android/brulion-selftest-<version>-x86_64.apk
# Environment (optional):
#   ANDROID_SDK_ROOT         Android SDK (default ~/.cache/brulion-android-sdk); the emulator and
#                            the system image are installed with its sdkmanager when missing
#   BRULION_EMULATOR_IMAGE   default "system-images;android-36;google_apis;x86_64" (--keyboard,
#                            --selection and --folders also run on older ones, e.g. android-30)
#   BRULION_EMULATOR_PORT    console port (default 5584; adb serial emulator-<port>; 5554-5584:
#                            the emulator warns that adb may not work with others)
#   BRULION_EMULATOR_OUT     logs and screenshots (default dist-android/emulator-test)
#   BRULION_WEBPICTURE_HTTPS --web-pictures: the public https picture (default: Google's logo)
#   BRULION_WEBPICTURE_PORT  --web-pictures: the host's port of the http picture (default 8765)
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
SHARE=0
WIDGETS=0
WEBPICTURES=0
DONE=0
if [[ ${1:-} == --keyboard ]]; then
  KEYBOARD=1
  shift
elif [[ ${1:-} == --selection ]]; then
  SELECTION=1
  shift
elif [[ ${1:-} == --folders ]]; then
  FOLDERS=1
  shift
elif [[ ${1:-} == --share ]]; then
  SHARE=1
  shift
elif [[ ${1:-} == --widgets ]]; then
  WIDGETS=1
  shift
elif [[ ${1:-} == --done ]]; then
  DONE=1
  shift
elif [[ ${1:-} == --web-pictures ]]; then
  WEBPICTURES=1
  shift
fi
APP_APK=${1:-dist-android/brulion-$VERSION-x86_64.apk}
SELFTEST_APK=${2:-dist-android/brulion-selftest-$VERSION-x86_64.apk}
[[ -f $APP_APK ]] || die "$APP_APK not found (build it: BRULION_ANDROID_ABIS=x86_64 BRULION_ANDROID_SELFTEST=1 scripts/android-build.sh)"
((KEYBOARD || SELECTION || FOLDERS || SHARE || WIDGETS || WEBPICTURES || DONE)) || [[ -f $SELFTEST_APK ]] || die "$SELFTEST_APK not found"

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
http_pid=""
cleanup() {
  [[ -z $http_pid ]] || kill "$http_pid" 2>/dev/null || true
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
not_running() { ! running; }
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

# --- items on screen found by their names (every mode but the self-test) ---------------------------

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
  # Its first paragraph, found by its text (the page's tag chips, "Widok tagu" and the properties
  # above it move it down — a blind tap opened the tag's tasks); else a few heights 25–45 % down
  # the screen (the system bars differ between versions).
  paragraph_xy=$(ui_find_all 'Brulion to notatnik w zwykłych plikach Markdown[^"]*' | head -n 1)
  if [[ -n $paragraph_xy ]]; then
    adb shell input tap $paragraph_xy
    wait_for 6 ime_shown || true
  else
    for fraction in 40 30 35 45 27; do
      adb shell input tap $((SCREEN_W * 30 / 100)) $((SCREEN_H * fraction / 100))
      wait_for 6 ime_shown && break
    done
  fi
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

# --- "Udostępnij → Brulion" (--share) -----------------------------------------------------------

SHARE_ACTIVITY=$PKG/$PKG.android.ShareActivity
# No copy left in the share inbox (files/share-in/<stamp>/…) once a share is saved.
inbox_empty() { [[ -z $(adb shell run-as "$PKG" find files/share-in -type f 2>/dev/null | tr -d '\r') ]]; }
# A notebook file byte for byte (through base64: no line ending conversion; not `exec-out`, which
# passes the quotes of a path with spaces on to run-as).
vault_bytes() { adb shell run-as "$PKG" base64 "\"$VAULT/$1\"" 2>/dev/null | tr -d '\r\n' | base64 -d; } # <file>
vault_same() { vault_bytes "$1" | cmp -s - "$2"; } # <file> <local>
# A file pushed to the shared Download folder, as the MediaStore content:// URI another app shares.
media_uri() { # <file name>
  local id
  id=$(adb shell content query --uri content://media/external/file --projection _id \
    --where "\"_display_name='$1'\"" 2>/dev/null | sed -nE 's/.*_id=([0-9]+).*/\1/p' | head -n 1)
  [[ -n $id ]] && echo "content://media/external/file/$id"
}

if ((SHARE)); then
  info "Share to Brulion: $APP_APK on ${SCREEN_W}x$SCREEN_H, density $DENSITY, API $android_api"
  adb install -r -g "$APP_APK" >/dev/null
  adb shell run-as "$PKG" true 2>/dev/null || die "$APP_APK is not debuggable (BRULION_ANDROID_BUILD_TYPE=Debug)"
  # Reading a file another app put on shared storage through MediaStore.
  adb shell appops set "$PKG" MANAGE_EXTERNAL_STORAGE allow 2>/dev/null || true
  today_journal="journals/$(adb shell date +%Y-%m-%d | tr -d '\r').md"
  start_app '--demo --locale pl --route journals'
  check "the app is running" running

  # 1. Text while the app runs: the dialog, today's journal by default, no date parsing.
  adb shell am start -n "$SHARE_ACTIVITY" -a android.intent.action.SEND -t text/plain \
    --es android.intent.extra.SUBJECT "'Artykuł na jutro'" \
    --es android.intent.extra.TEXT "'Zażółć gęślą jaźń https://example.org/łódź'" >/dev/null
  check "the share dialog opens" wait_for 20 ui_has 'Zapisz w Brulionie'
  app_shot sh-1-text-dialog
  check "Zapisz saves it" ui_tap 'Zapisz' last
  check "a block of today's journal: the link with the title, no reminder" \
    wait_for 10 vault_grep "$today_journal" '^- \[Artykuł na jutro\]\(https://example.org/łódź\) — Zażółć gęślą jaźń$'
  app_shot sh-2-text-saved

  # 2. A .md file after a cold start: a new page by default, its bytes kept.
  printf '# Lista zakupów\r\n\r\n- [ ] mleko\r\n- [ ] chleb ąę\r\n' >"$OUT/Lista zakupów.md"
  adb push "$OUT/Lista zakupów.md" "/sdcard/Download/Lista zakupów.md" >/dev/null
  adb shell content call --uri content://media --method scan_volume --arg external_primary >/dev/null 2>&1 ||
    adb shell am broadcast -a android.intent.action.MEDIA_SCANNER_SCAN_FILE \
      -d "'file:///sdcard/Download/Lista zakupów.md'" >/dev/null
  sleep 3
  uri=$(media_uri 'Lista zakupów.md' || true)
  echo "    the shared file: ${uri:-not in MediaStore}"
  check "MediaStore lists the pushed file" test -n "$uri"
  adb shell am force-stop "$PKG"
  adb logcat -c
  adb shell am start -n "$SHARE_ACTIVITY" -a android.intent.action.SEND -t text/markdown \
    --eu android.intent.extra.STREAM "$uri" --grant-read-uri-permission >/dev/null
  # (A cold start has no --locale pl: the app speaks the phone's language.)
  check "a cold start opens the share dialog" wait_for 40 ui_has 'Zapisz w Brulionie|Save to Brulion'
  check "the file is listed" ui_has 'Lista zakupów.md'
  app_shot sh-3-file-dialog
  check "Zapisz saves it" ui_tap 'Zapisz|Save' last
  check "a new page named after the file" wait_for 10 vault_has 'Lista zakupów.md'
  check "its bytes kept (CRLF too)" vault_same 'Lista zakupów.md' "$OUT/Lista zakupów.md"
  vault_bytes 'Lista zakupów.md' >"$OUT/Lista zakupów (saved).md" || true
  check "the copies in files/share-in are gone" inbox_empty
  app_shot sh-4-file-saved

  # 3. A picture: not offered (text/* only), and refused when sent to the activity anyway.
  adb shell am start -n "$SHARE_ACTIVITY" -a android.intent.action.SEND -t image/png \
    --es android.intent.extra.TEXT "''" >/dev/null 2>&1 || true
  sleep 3
  check "nothing to save: no dialog" ui_lacks 'Zapisz w Brulionie'
  check "the share sheet offers Brulion for text" bash -c \
    "'$SDK/platform-tools/adb' -s $SERIAL shell cmd package query-activities -a android.intent.action.SEND -t text/plain | grep -q ShareActivity"
  check "but not for pictures" bash -c \
    "! '$SDK/platform-tools/adb' -s $SERIAL shell cmd package query-activities -a android.intent.action.SEND -t image/png | grep -q ShareActivity"

  # 4. Several files at once from the system's file manager (ACTION_SEND_MULTIPLE, which `am` cannot
  # send): two text files and a picture. The selection's type is */*, which the share sheet may
  # match with Brulion's text/* filter: then the dialog lists the picture as not a text file;
  # otherwise the picture is left out of the selection. Either way the text files go to today's
  # journal, one block per file.
  printf 'Notatka ąę\nwiersz 2\n' >"$OUT/Notatka źdźbło.txt"
  adb push "$OUT/Notatka źdźbło.txt" "/sdcard/Download/Notatka źdźbło.txt" >/dev/null
  adb exec-out screencap -p >"$OUT/obrazek.png"
  adb push "$OUT/obrazek.png" /sdcard/Download/obrazek.png >/dev/null
  adb shell content call --uri content://media --method scan_volume --arg external_primary >/dev/null 2>&1 || true
  sleep 2
  adb shell am start -a android.intent.action.VIEW -t vnd.android.document/root \
    -d content://com.android.providers.downloads.documents/root/downloads >/dev/null
  check "the file manager lists the files" wait_for 15 ui_has 'Notatka źdźbło\.txt'
  xy=$(ui_find_all 'Lista zakupów\.md' | head -n 1)
  [[ -z $xy ]] || long_press $xy
  sleep 2
  ui_tap 'Notatka źdźbło\.txt' first 1 || true
  ui_tap 'obrazek\.png' first 1 || true
  check "three files selected" ui_has '3 selected|Zaznaczono: 3|3 wybrane'
  share_sheet() { # the share sheet of the selection, expanded
    ui_tap 'Share|Udostępnij' first 4 || return 1
    adb shell input swipe $((SCREEN_W / 2)) $((SCREEN_H * 70 / 100)) $((SCREEN_W / 2)) $((SCREEN_H * 25 / 100)) 300
    sleep 2
  }
  check "the share sheet opens" share_sheet
  app_shot sh-5-share-sheet
  with_picture=0
  if ui_has 'Brulion'; then
    with_picture=1
    echo "    the share sheet offers Brulion for the selection with the picture (*/*)"
  else
    echo "    the share sheet leaves Brulion out for the selection with the picture: the picture deselected"
    adb shell input keyevent 4
    sleep 2
    ui_tap 'obrazek\.png' first 1 || true
    check "two text files selected" ui_has '2 selected|Zaznaczono: 2|2 wybrane'
    check "the share sheet opens" share_sheet
  fi
  check "… and offers Brulion" ui_tap 'Brulion' first 4
  check "the share dialog lists the text files" wait_for 30 ui_has 'Notatka źdźbło\.txt'
  check "… the .md one too" ui_has 'Lista zakupów\.md'
  if ((with_picture)); then
    check "… and the picture, which it cannot save" ui_has 'obrazek\.png'
  fi
  app_shot sh-6-files-dialog
  check "Zapisz saves them" ui_tap 'Zapisz|Save' last
  check "one block per file: the .md" wait_for 10 vault_grep "$today_journal" '^- \*\*Lista zakupów\*\*$'
  check "… and the .txt with its lines" bash -c "grep -A2 -x -- '- \*\*Notatka źdźbło\*\*' <<<\"\$1\" | grep -qx '  wiersz 2'" _ "$(vault_cat "$today_journal")"
  check "… nothing of the picture" bash -c "! grep -q obrazek <<<\"\$1\"" _ "$(vault_cat "$today_journal")"
  check "the copies in files/share-in are gone" wait_for 6 inbox_empty
  app_shot sh-7-files-saved

  adb logcat -d >"$OUT/share-logcat.txt"
  check "no crash" bash -c "! grep -E 'FATAL EXCEPTION|Fatal signal' '$OUT/share-logcat.txt'"
  check "no QML errors" bash -c "! grep -E 'TypeError|ReferenceError|Binding loop detected' '$OUT/share-logcat.txt'"
  info "Result"
  echo "logs and screenshots: $(cd "$OUT" && pwd)"
  ((failures == 0)) || die "$failures check(s) failed"
  echo "all checks passed"
  exit 0
fi

# --- the home-screen widgets (--widgets) ---------------------------------------------------------

QUICK_ADD=$PKG/$PKG.android.QuickAddActivity
WIDGET_RECEIVER=$PKG/$PKG.android.WidgetReceiver
WIDGET_PREFS=shared_prefs/$PKG.android.widgets.xml
prefs_grep() { adb shell run-as "$PKG" cat "$1" 2>/dev/null | grep -q -- "$2"; } # <file> <text>
# Qt's library is not loaded in the app's process (quick add and the widgets run without it).
qt_not_loaded() {
  local pid
  pid=$(adb shell pidof "$PKG" | tr -d '\r')
  [[ -n $pid ]] && [[ $(adb shell "grep -c libbrulion /proc/$pid/maps" | tr -d '\r') == 0 ]]
}
home() { adb shell input keyevent 3 && sleep 1 && adb shell input keyevent 3 && sleep 2; }
# The home screen's page with the item named so (the launcher puts each new widget where it fits).
show_page() { # <ERE>
  local _
  home
  for _ in 1 2 3 4; do
    ui_has "$1" && return 0
    adb shell input swipe $((SCREEN_W * 85 / 100)) $((SCREEN_H / 2)) $((SCREEN_W * 15 / 100)) $((SCREEN_H / 2)) 300
    sleep 2
  done
  ui_has "$1"
}
# A widget placed from the launcher's picker: long press on the home screen, "Widgets", Brulion's
# entry, a tap on the widget's preview (named "<label> widget, …") and its "Add".
place_widget() { # <label ERE>
  local y found=0
  # An empty spot of the home screen (a long press on a widget would pick the widget up).
  for y in 52 66 47 74 58; do
    home
    adb shell input swipe $((SCREEN_W / 2)) $((SCREEN_H * y / 100)) $((SCREEN_W / 2)) $((SCREEN_H * y / 100)) 1300
    sleep 2
    if ui_has 'Widgets|Widżety'; then
      found=1
      break
    fi
    adb shell input keyevent 4
  done
  ((found)) || return 1
  ui_tap 'Widgets|Widżety' || return 1
  ui_tap 'Brulion' || return 1
  # The previews of a tall widget may start below the fold: the picker's list scrolled up.
  for y in 1 2 3; do
    ui_has "($1) widget.*" && break
    adb shell input swipe $((SCREEN_W / 2)) $((SCREEN_H * 75 / 100)) $((SCREEN_W / 2)) $((SCREEN_H * 40 / 100)) 400
    sleep 2
  done
  ui_tap "($1) widget.*" || return 1
  ui_tap "(Add|Dodaj) ($1) widget" first 4 || return 1
  home
}
# The launcher's resize frame of the widget named so moved by its bottom handle by <dy> px.
resize_widget() { # <ERE of a text in the widget> <dy>
  local xy handle
  xy=$(ui_find_all "$1" | head -n 1)
  [[ -n $xy ]] || return 1
  adb shell input swipe $xy $xy 1500
  sleep 2
  ui_dump
  handle=$(sed 's/<node /\n<node /g' "$OUT/ui.xml" | grep 'widget_resize_bottom_handle' |
    sed -nE 's/.*bounds="\[([0-9]+),([0-9]+)\]\[([0-9]+),([0-9]+)\]".*/\1 \2 \3 \4/p' | awk '{ print int(($1 + $3) / 2), int(($2 + $4) / 2) }')
  [[ -n $handle ]] || return 1
  adb shell input swipe $handle "${handle% *}" $((${handle#* } + $2)) 1500
  sleep 2
  adb shell input keyevent 4
  sleep 3
}
refresh_widgets() { adb shell am broadcast -a "$PKG.android.action.WIDGETS_MIDNIGHT" -n "$WIDGET_RECEIVER" >/dev/null; sleep 3; }
quick_add() { # <text> [chips to tap…]: QuickAddActivity with the text, the chips, "Dodaj"
  # ("Add": the dialog speaks the phone's language until the app published a snapshot)
  local chip
  adb shell am start -n "$QUICK_ADD" --es android.intent.extra.TEXT "'$1'" >/dev/null
  wait_for 15 ui_has 'Dodaj|Add|ADD' || return 1
  shift
  for chip in "$@"; do
    if [[ $chip == OK ]]; then ui_tap 'OK' || return 1; else ui_tap "$chip" || return 1; fi
  done
  ui_tap 'Dodaj|Add|ADD'
}

if ((WIDGETS)); then
  info "Widgets: $APP_APK on ${SCREEN_W}x$SCREEN_H, density $DENSITY, API $android_api"
  adb install -r -g "$APP_APK" >/dev/null
  adb root >/dev/null 2>&1 || true
  sleep 3
  adb wait-for-device
  adb shell run-as "$PKG" true 2>/dev/null || die "$APP_APK is not debuggable (BRULION_ANDROID_BUILD_TYPE=Debug)"
  adb shell am start -n "$QUICK_ADD" >/dev/null 2>&1 && adb shell am force-stop "$PKG" ||
    die "cannot start the (not exported) QuickAddActivity: a google_apis image with adb root is needed"
  today=$(adb shell date +%Y-%m-%d | tr -d '\r')
  tomorrow=$(adb shell date -d @$(($(adb shell date +%s | tr -d '\r') + 86400)) +%Y-%m-%d | tr -d '\r')
  journal="journals/$today.md"

  # 1. The app publishes the snapshot (the sample notebook) when it leaves the front.
  start_app '--demo --locale pl --route journals'
  check "the app is running" running
  home
  check "the snapshot of the notebook is stored" wait_for 15 prefs_grep "$WIDGET_PREFS" 'Przykładowy brulion'
  check "the app says so" log_has 'Snapshot published'

  # 2. The three widgets from the launcher's picker.
  check "the tasks widget is placed" place_widget 'Tasks — My day|Zadania — Mój dzień'
  check "the calendar widget is placed" place_widget 'Calendar|Kalendarz'
  check "the notes widget is placed" place_widget 'Notes|Notatki'
  adb shell dumpsys appwidget >"$OUT/dumpsys-appwidget.txt"
  check "the system binds the three providers" bash -c \
    "grep -q TasksWidget '$OUT/dumpsys-appwidget.txt' && grep -q CalendarWidget '$OUT/dumpsys-appwidget.txt' && grep -q NotesWidget '$OUT/dumpsys-appwidget.txt'"
  check "the tasks widget shows the day in the app's language" show_page 'Mój dzień'
  app_shot wg-1-tasks-light

  # 3. Quick add with the app killed: written at once, Qt never starts.
  adb shell am force-stop "$PKG"
  adb logcat -c
  check "quick add: a task for tomorrow (Jutro)" quick_add 'Kupić żółty ser' 'Jutro'
  check "it says it was added" wait_for 10 ui_has 'Dodano do dzisiejszego dziennika.*'
  app_shot wg-2-quick-add
  check "the line is in today's journal" vault_grep "$journal" "^- \[ \] Kupić żółty ser 📅 $tomorrow\$"
  check "Qt was not loaded for it" qt_not_loaded
  check "quick add: a task for today (Dziś)" quick_add 'Odebrać paczkę' 'Dziś'
  check "quick add: a time (Godzina… 09:00) tomorrow with Alarm" quick_add 'Pobudka na pociąg' 'Godzina…' OK 'Jutro' 'Alarm'
  check "the alarm line is in today's journal" vault_grep "$journal" "^- \[ \] Pobudka na pociąg ⏰! $tomorrow 09:00\$"
  check "a provisional native reminder is armed for it" prefs_grep "shared_prefs/$PKG.android.reminders.xml" '&quot;provisional&quot;:true'
  adb shell input keyevent 4
  adb shell input keyevent 4
  check "the tasks widget shows today's new task at once" show_page 'Odebrać paczkę'
  app_shot wg-3-tasks-added

  # 3b. A notebook folder that is gone (moved, deleted, not mounted) is never made again: the
  # entry waits for the app instead.
  adb shell am force-stop "$PKG"
  adb shell run-as "$PKG" mv "\"$VAULT\"" "\"$VAULT-moved\""
  check "quick add with the notebook's folder gone" quick_add 'Zadanie bez folderu' 'Bez terminu'
  check "it waits for the app" wait_for 10 ui_has 'Zapisze się w dzienniku po otwarciu Brulionu.*'
  check "the notebook's folder is not made again" vault_lacks ''
  adb shell input keyevent 4
  adb shell input keyevent 4
  adb shell run-as "$PKG" mv "\"$VAULT-moved\"" "\"$VAULT\""

  # 4. Without a snapshot the entry waits for the app, which writes it at its start.
  adb shell am force-stop "$PKG"
  adb shell run-as "$PKG" rm -f "$WIDGET_PREFS"
  check "quick add without a snapshot" quick_add 'Zadanie z kolejki'
  check "it waits for the app (in the phone's language: no snapshot names the app's)" wait_for 10 ui_has \
    'It will be written to the journal when Brulion opens.*'
  adb shell input keyevent 4
  adb shell input keyevent 4
  start_app '--demo --locale pl --route journals'
  check "the app wrote the queued entry" wait_for 20 vault_grep "$journal" '^- \[ \] Zadanie z kolejki 📅 '
  check "… and the one queued while the folder was gone" vault_grep "$journal" '^- \[ \] Zadanie bez folderu$'
  check "the app's own reminder replaced the provisional one" bash -c \
    "! '$SDK/platform-tools/adb' -s $SERIAL shell run-as $PKG cat shared_prefs/$PKG.android.reminders.xml | grep -q '&quot;provisional&quot;:true'"

  # 4b. The app alive in the background: quick add writes itself; back in front the app reads the
  # notebook and hands its reminders over again, the provisional one gives way.
  home
  adb logcat -c
  check "quick add with the app in the background" quick_add 'Wizyta u dentysty' 'Godzina…' OK 'Jutro'
  check "… written at once" wait_for 10 vault_grep "$journal" "^- \[ \] Wizyta u dentysty ⏰ $tomorrow 09:00\$"
  adb shell input keyevent 4
  adb shell input keyevent 4
  adb shell am start -n "$ACTIVITY" >/dev/null
  check "back in front the app hands its reminders over again" wait_for 20 log_has 'handing the reminders over again'
  check "… and no provisional reminder is left" wait_for 10 bash -c \
    "! '$SDK/platform-tools/adb' -s $SERIAL shell run-as $PKG cat shared_prefs/$PKG.android.reminders.xml | grep -q '&quot;provisional&quot;:true'"

  # 5. A tap on a task of the widget opens its note (a cold start).
  adb shell am force-stop "$PKG"
  refresh_widgets
  show_page 'Odebrać paczkę' >/dev/null || true
  adb logcat -c
  check "a tap on the widget's task" ui_tap 'Odebrać paczkę' first 20
  check "Brulion opens" wait_for 20 bash -c \
    "'$SDK/platform-tools/adb' -s $SERIAL shell dumpsys activity activities | grep -E 'topResumedActivity|mResumedActivity' | grep -q BrulionActivity"
  app_shot wg-4-tap-opens-note

  # 6. The calendar: the month grid and its buttons; then the widgets in the dark, bigger and smaller.
  # (Opened by the tap the app ran in the phone's language: back to Polish, which the widgets follow.)
  start_app '--demo --locale pl --route journals'
  home
  wait_for 15 log_has 'Snapshot published' || true
  check "the calendar widget shows its month" show_page 'Następny miesiąc'
  app_shot wg-5-calendar-light
  months=(Styczeń Luty Marzec Kwiecień Maj Czerwiec Lipiec Sierpień Wrzesień Październik Listopad Grudzień)
  this_month=$(adb shell date +%-m | tr -d '\r')
  this_year=$(adb shell date +%Y | tr -d '\r')
  check "the month's title" ui_has "${months[this_month - 1]} $this_year"
  check "› shows the next month" ui_tap 'Następny miesiąc'
  if ((this_month == 12)); then next_title="Styczeń $((this_year + 1))"; else next_title="${months[this_month]} $this_year"; fi
  check "… $next_title" wait_for 10 ui_has "$next_title"
  app_shot wg-6-calendar-next-month
  check "‹ goes back" ui_tap 'Poprzedni miesiąc'
  check "the notes widget shows the notebook" show_page 'Szybka notatka'
  app_shot wg-7-notes-light
  adb shell cmd uimode night yes >/dev/null
  sleep 2
  refresh_widgets
  show_page 'Mój dzień' >/dev/null || true
  app_shot wg-8-tasks-dark
  resize_widget 'Mój dzień' $((SCREEN_H * 18 / 100)) || true
  check "… tomorrow's tasks in the taller tasks widget" wait_for 10 ui_has 'JUTRO · [0-9]+'
  app_shot wg-9-tasks-tall-dark
  show_page 'Następny miesiąc' >/dev/null || true
  app_shot wg-10-calendar-dark
  resize_widget 'Następny miesiąc' $((-SCREEN_H * 30 / 100)) || true
  check "the calendar widget made lower shows the agenda only" wait_for 10 ui_has 'Kalendarz'
  app_shot wg-11-calendar-low-dark
  show_page 'Szybka notatka' >/dev/null || true
  app_shot wg-12-notes-dark
  adb shell cmd uimode night no >/dev/null
  refresh_widgets
  show_page 'Kalendarz' >/dev/null || true
  app_shot wg-13-calendar-low-light
  show_page 'Mój dzień' >/dev/null || true
  app_shot wg-14-tasks-tall-light

  adb logcat -d >"$OUT/widgets-logcat.txt"
  check "no crash" bash -c "! grep -E 'FATAL EXCEPTION|Fatal signal' '$OUT/widgets-logcat.txt'"
  info "Result"
  echo "    the journal of $today:"
  vault_cat "$journal" | tail -n 5 | sed 's/^/      /'
  echo "logs and screenshots: $(cd "$OUT" && pwd)"
  ((failures == 0)) || die "$failures check(s) failed"
  echo "all checks passed"
  exit 0
fi

# --- "Zrobione" in the app's own reminders (--done) -----------------------------------------------

if ((DONE)); then
  info "Done from a notification: $APP_APK on ${SCREEN_W}x$SCREEN_H, density $DENSITY, API $android_api"
  adb install -r -g "$APP_APK" >/dev/null
  adb shell run-as "$PKG" true 2>/dev/null || die "$APP_APK is not debuggable (BRULION_ANDROID_BUILD_TYPE=Debug)"
  # Two tasks with reminders (the phone's wall clock): 2 and 4 minutes ahead, whole minutes.
  zone=$(adb shell getprop persist.sys.timezone | tr -d '\r')
  now=$(adb shell date +%s | tr -d '\r')
  at() { TZ=${zone:-UTC} date -d "@$((now + $1 * 60))" '+%Y-%m-%d %H:%M'; } # <minutes ahead>
  today=$(TZ=${zone:-UTC} date -d "@$now" +%Y-%m-%d)
  first=$(at 2)
  second=$(at 4)
  DN_VAULT=files/Zadania
  dn_journal="$DN_VAULT/journals/$today.md"
  printf -- '- [ ] Podlać kwiaty ⏰ %s\n- [ ] Zadzwonić do Łucji ⏰ %s\n- Koniec dnia\n' "$first" "$second" >"$OUT/journal.md"
  adb push "$OUT/journal.md" /data/local/tmp/brulion-journal.md >/dev/null
  adb shell run-as "$PKG" mkdir -p "$DN_VAULT/journals"
  adb shell run-as "$PKG" cp /data/local/tmp/brulion-journal.md "$dn_journal"
  dn_cat() { adb shell run-as "$PKG" cat "$dn_journal" 2>/dev/null | tr -d '\r'; }
  dn_line() { dn_cat | grep -qxF -- "$1"; } # <the whole line>
  notified() { adb shell dumpsys notification --noredact | grep -q "$1"; } # <title>
  shade_done() { # "Zrobione" in the notification <title> (the only one in the shade)
    adb shell cmd statusbar expand-notifications >/dev/null 2>&1 || true
    sleep 2
    app_shot "dn-shade-$2"
    ui_tap 'Zrobione|ZROBIONE|Done|DONE'
    local ok=$?
    adb shell cmd statusbar collapse >/dev/null 2>&1 || true
    return $ok
  }
  echo "    reminders at $first and $second ($zone)"
  start_app "--vault /data/user/0/$PKG/$DN_VAULT --locale pl --route journals"
  check "the app is running" running
  app_shot dn-1-journal
  check "the first reminder is armed natively" bash -c \
    "'$SDK/platform-tools/adb' -s $SERIAL shell run-as $PKG cat shared_prefs/$PKG.android.reminders.xml | grep -q 'Podlać kwiaty'"
  # 1. The app on screen: written at once.
  check "the first reminder fires" wait_for 150 notified 'Podlać kwiaty'
  check "'Zrobione' tapped with the app on screen" shade_done 'Podlać kwiaty' first
  check "the task is done in the note at once (✅ the day of the tap)" wait_for 15 dn_line \
    "- [x] Podlać kwiaty ⏰ $first ✅ $today"
  check "… its notification is gone" wait_for 6 bash -c "! '$SDK/platform-tools/adb' -s $SERIAL shell dumpsys notification --noredact | grep -q 'Podlać kwiaty'"
  sleep 3
  app_shot dn-2-done-at-once
  # 2. The app's process gone: queued, written at the next start.
  adb shell input keyevent 3
  sleep 2
  adb shell am kill "$PKG"
  check "the app's process is gone (am kill)" wait_for 6 not_running
  check "the second reminder fires with the app closed" wait_for 200 notified 'Zadzwonić do Łucji'
  check "'Zrobione' tapped with the app closed" shade_done 'Zadzwonić do Łucji' second
  check "… the note is not changed yet" dn_line "- [ ] Zadzwonić do Łucji ⏰ $second"
  check "… the tap waits in the queue" bash -c \
    "'$SDK/platform-tools/adb' -s $SERIAL shell run-as $PKG cat shared_prefs/$PKG.android.done.xml | grep -q 'Zadzwonić do Łucji'"
  start_app "--vault /data/user/0/$PKG/$DN_VAULT --locale pl --route journals"
  check "the next start writes it (✅ the day of the tap)" wait_for 20 dn_line \
    "- [x] Zadzwonić do Łucji ⏰ $second ✅ $today"
  check "… the rest of the note untouched" dn_line "- Koniec dnia"
  app_shot dn-3-done-at-start

  adb logcat -d >"$OUT/done-logcat.txt"
  check "no crash" bash -c "! grep -E 'FATAL EXCEPTION|Fatal signal' '$OUT/done-logcat.txt'"
  check "no QML errors" bash -c "! grep -E 'TypeError|ReferenceError|Binding loop detected' '$OUT/done-logcat.txt'"
  info "Result"
  echo "    the journal:"
  dn_cat | sed 's/^/      /'
  echo "logs and screenshots: $(cd "$OUT" && pwd)"
  ((failures == 0)) || die "$failures check(s) failed"
  echo "all checks passed"
  exit 0
fi

# --- pictures from the internet (--web-pictures) -------------------------------------------------

if ((WEBPICTURES)); then
  info "Web pictures: $APP_APK on ${SCREEN_W}x$SCREEN_H, density $DENSITY, API $android_api"
  adb install -r -g "$APP_APK" >/dev/null
  adb shell run-as "$PKG" true 2>/dev/null || die "$APP_APK is not debuggable (BRULION_ANDROID_BUILD_TYPE=Debug)"
  https_url=${BRULION_WEBPICTURE_HTTPS:-https://www.google.com/images/branding/googlelogo/2x/googlelogo_color_272x92dp.png}
  http_port=${BRULION_WEBPICTURE_PORT:-8765}
  # The http picture: a 640×400 gradient (PNG) served from the host (10.0.2.2 in the emulator).
  mkdir -p "$OUT/www"
  python3 - "$OUT/www/gradient.png" <<'PY'
import struct, sys, zlib
w, h = 640, 400
raw = b"".join(b"\0" + bytes(v for x in range(w) for v in (x * 255 // w, y * 255 // h, 160)) for y in range(h))
chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
open(sys.argv[1], "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                              + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
PY
  python3 -m http.server "$http_port" --bind 127.0.0.1 --directory "$OUT/www" >"$OUT/http-server.log" 2>&1 &
  http_pid=$!
  sleep 1
  kill -0 "$http_pid" 2>/dev/null || die "the http server did not start on port $http_port (BRULION_WEBPICTURE_PORT)"
  # The notebook: today's journal with both pictures, each a block of its own.
  WP_VAULT=files/Obrazy
  wp_journal="$WP_VAULT/journals/$(adb shell date +%Y-%m-%d | tr -d '\r').md"
  printf -- '- Zdjęcie przez https:\n- ![Logo przez https](%s)\n- Zdjęcie przez http:\n- ![Gradient przez http](http://10.0.2.2:%s/gradient.png)\n- Koniec strony ąćęłńóśźż\n' \
    "$https_url" "$http_port" >"$OUT/journal.md"
  adb push "$OUT/journal.md" /data/local/tmp/brulion-journal.md >/dev/null
  adb shell run-as "$PKG" mkdir -p "$WP_VAULT/journals"
  adb shell run-as "$PKG" cp /data/local/tmp/brulion-journal.md "$wp_journal"
  cached() { [[ $(adb shell run-as "$PKG" ls cache/web-pictures 2>/dev/null | tr -d '\r' | grep -c .) -ge $1 ]]; } # <n>
  airplane() { adb shell cmd connectivity airplane-mode "$1" >/dev/null; sleep 4; } # enable|disable
  wp_start() { start_app "--vault /data/user/0/$PKG/$WP_VAULT --locale pl --route journals"; }
  wp_cat() { adb shell run-as "$PKG" cat "$wp_journal" 2>/dev/null | tr -d '\r'; }
  in_front() { adb shell dumpsys activity activities | grep -m 1 topResumedActivity | grep -qE "$1"; } # <ERE>
  asking() { [[ $(ui_find_all 'Pokaż' | sort -u | wc -l) -eq $1 ]]; } # <n>: pictures waiting for Pokaż

  wp_start
  check "the app is running" running
  check "both pictures wait for Pokaż (asking first is the default)" asking 2
  app_shot wp-1-asking
  # 1. Pokaż: the https picture and the http one (Qt's own sockets: cleartextTrafficPermitted is
  # Java's policy and does not apply).
  check "Pokaż fetches the https picture" ui_tap 'Pokaż' first 1
  check "… into the cache" wait_for 20 cached 1
  check "Pokaż fetches the http picture" ui_tap 'Pokaż' first 1
  check "… into the cache (http:// is not blocked)" wait_for 20 cached 2
  check "… from the host's server" grep -q 'GET /gradient.png HTTP/1.1" 200' "$OUT/http-server.log"
  check "both shown" wait_for 10 ui_lacks 'Pokaż'
  app_shot wp-2-shown
  # 2. A restart without network: both from the cache, nothing asked.
  airplane enable
  wp_start
  check "offline after a restart: both pictures shown from the cache" ui_lacks 'Pokaż'
  app_shot wp-3-offline-cached
  # 3. A tap opens the viewer: actual size and back, Back closes it.
  picture_xy=$(ui_find_all 'Gradient przez http' | tail -n 1)
  echo "    the http picture at $picture_xy"
  adb shell input tap $picture_xy
  sleep 2
  check "a tap opens the viewer" wait_for 10 ui_has 'Zamknij'
  app_shot wp-4-viewer
  check "… actual size" ui_tap 'Pokaż naturalny rozmiar'
  check "… and fit again" ui_tap 'Dopasuj do widoku'
  adb shell input keyevent 4
  check "Back closes the viewer" wait_for 10 ui_lacks 'Zamknij'
  check "… and stays in the journal" ui_has 'Gradient przez http'
  # 4. A long press opens the picture's menu.
  long_press $picture_xy
  sleep 2
  check "a long press opens the menu" ui_has 'Obraz z 10\.0\.2\.2'
  check "… Powiększ, Zapisz w notatniku, Otwórz w przeglądarce, Kopiuj adres obrazu" bash -c \
    "grep -q 'content-desc=\"Powiększ\"' '$OUT/ui.xml' && grep -q 'content-desc=\"Zapisz w notatniku\"' '$OUT/ui.xml' &&
     grep -q 'content-desc=\"Otwórz w przeglądarce\"' '$OUT/ui.xml' && grep -q 'content-desc=\"Kopiuj adres obrazu\"' '$OUT/ui.xml'"
  app_shot wp-5-menu
  check "Powiększ opens the viewer" ui_tap 'Powiększ'
  check "… Zamknij closes it" ui_tap 'Zamknij'
  check "… closed" wait_for 10 ui_lacks 'Zamknij'
  # 5. Cache cleared, no network: Pokaż says why; Spróbuj ponownie once the network is back.
  adb shell run-as "$PKG" rm -rf cache/web-pictures
  wp_start
  check "the cleared cache asks again" asking 2
  check "Pokaż without network" ui_tap 'Pokaż' first 4
  check "… fails with a reason (no connection)" wait_for 15 log_has 'Picture from .* not loaded: network error'
  check "… and offers Spróbuj ponownie" ui_has 'Spróbuj ponownie'
  app_shot wp-6-offline-error
  airplane disable
  check "Spróbuj ponownie with the network back" ui_tap 'Spróbuj ponownie' first 1
  check "… fetched" wait_for 30 cached 1
  # 6. Zapisz w notatniku: the picture in assets/, the note's link to it.
  check "Zapisz w notatniku (the http picture)" ui_tap 'Zapisz w notatniku' last 4
  check "… the picture saved in the notebook" wait_for 15 bash -c \
    "'$SDK/platform-tools/adb' -s $SERIAL shell run-as $PKG ls $WP_VAULT/assets/gradient.png >/dev/null 2>&1"
  check "… the note links to the copy" bash -c "grep -q '!\[Gradient przez http\](assets/gradient.png)' <<<\"\$1\"" _ "$(wp_cat)"
  check "… the https picture's link untouched" bash -c "grep -qF '![Logo przez https]($https_url)' <<<\"\$1\"" _ "$(wp_cat)"
  app_shot wp-7-saved
  # 7. Otwórz w przeglądarce hands the address to a browser.
  long_press $(ui_find_all 'Logo przez https' | tail -n 1)
  sleep 2
  check "Otwórz w przeglądarce (the https picture)" ui_tap 'Otwórz w przeglądarce' first 4
  check "… a browser is in front" wait_for 10 in_front 'chrome|browser' 
  app_shot wp-8-browser

  adb logcat -d >"$OUT/web-pictures-logcat.txt"
  check "no crash" bash -c "! grep -E 'FATAL EXCEPTION|Fatal signal' '$OUT/web-pictures-logcat.txt'"
  check "no QML errors" bash -c "! grep -E 'TypeError|ReferenceError|Binding loop detected' '$OUT/web-pictures-logcat.txt'"
  info "Result"
  echo "    the journal:"
  wp_cat | sed 's/^/      /'
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
check "scheduleReminders round 1: 3 armed (past one skipped)" wait_for 20 log_has "SELFTEST schedule round=1 ok=1 scheduled=3 exact=1"
check "scheduleReminders round 2: idempotent" log_has "SELFTEST schedule round=2 ok=1 scheduled=3 exact=1"
check "permissions reported (notifications granted, exact alarms)" log_has "SELFTEST permissions(start) allFiles=0 notifications=1 exact=1"
check "cold-start tap delivered to C++" wait_for 10 log_has "SELFTEST activated path=dziennik/zimny-start.md line=5"
# A tap while the app runs: onNewIntent -> nativeActivated (Polish path).
adb shell am start -n "$ACTIVITY" --es "$EXTRA.PATH" "'strony/Łódź.md'" --ei "$EXTRA.LINE" 7 >/dev/null
check "tap while running delivered to C++ (Polish path)" wait_for 10 log_has "SELFTEST activated path=strony/Łódź.md line=7"
adb shell dumpsys alarm >"$OUT/dumpsys-alarm.txt"
check "AlarmManager holds Brulion's alarms" bash -c "grep -q '$PKG' '$OUT/dumpsys-alarm.txt'"
check "reminder 101 fired on channel 'reminders'" wait_for $((60 - (SECONDS - started))) notification_on 101 reminders
# "Zrobione" in the notification of the task reminder 101 with the app on screen (the shade pulled
# over it: Qt's event loop runs): the tap is queued natively and taken by C++ at once, the
# notification goes. (With the app in the background Qt's event loop is paused — no
# android.app.background_running — and C++ takes the queue when the app is back in front; with
# the process gone, at its next start: reminder 104 below.)
today=$(adb shell date +%Y-%m-%d | tr -d '\r')
tap_done() { # <reminder id>: "Zrobione" in its notification in the shade
  adb shell cmd statusbar expand-notifications >/dev/null 2>&1 || true
  sleep 2
  app_shot "shade-$1"
  check "'Zrobione' tapped in the notification of $1" ui_tap 'Zrobione|ZROBIONE|Done|DONE'
  adb shell cmd statusbar collapse >/dev/null 2>&1 || true
}
notification_gone() { ! adb shell dumpsys notification --noredact | grep -qE "pkg=$PKG .*id=$1 "; } # <id>
done_queued() { adb shell run-as "$PKG" cat "shared_prefs/$PKG.android.done.xml" 2>/dev/null | grep -q "selftest-$1"; }
tap_done 101
check "'Zrobione' reached C++ at once (key, note, line, the day of the tap)" wait_for 10 log_has \
  "SELFTEST done key=selftest-101 local=.* path=dziennik/2026-10-14.md line=3 day=$today"
check "notification 101 removed by 'Zrobione'" wait_for 6 notification_gone 101
adb shell input keyevent 3 # home: the alarm is posted while the app is in the background
check "alarm 102 rings on channel 'alarms'" wait_for $((95 - (SECONDS - started))) notification_on 102 alarms
adb exec-out screencap -p >"$OUT/alarm.png" || true
check "alarm 102 became 'missed-alarms' after its ring time (1 min)" wait_for $((170 - (SECONDS - started))) notification_on 102 missed-alarms
adb shell dumpsys notification --noredact >"$OUT/dumpsys-notification.txt"
adb logcat -d >"$OUT/selftest-logcat.txt"
# "Zrobione" with the app closed: the process killed (not force-stopped: its alarms stay), the task
# reminder 104 fires into a new process without Qt, the tap waits in the queue, and C++ takes it
# at the next start. The shade cleared first: next to the missed alarm 104 would be grouped
# (collapsed, without its buttons).
adb shell cmd statusbar expand-notifications >/dev/null 2>&1 || true
sleep 2
ui_tap 'Clear all|CLEAR ALL|Wyczyść wszystko|WYCZYŚĆ WSZYSTKO' >/dev/null || true
adb shell cmd statusbar collapse >/dev/null 2>&1 || true
adb shell am kill "$PKG"
sleep 2
check "the app's process is gone (am kill)" wait_for 6 not_running
check "reminder 104 fired with the app closed" wait_for $((230 - (SECONDS - started))) notification_on 104 reminders
tap_done 104
check "notification 104 removed by 'Zrobione'" wait_for 6 notification_gone 104
if adb shell run-as "$PKG" true 2>/dev/null; then # a debuggable APK: the queue can be read
  check "… the tap waits in the queue (DoneQueue)" wait_for 6 done_queued 104
fi
adb logcat -d >"$OUT/selftest-logcat.txt"
check "… nothing reached C++ yet" bash -c "! grep -q 'SELFTEST done key=selftest-104' '$OUT/selftest-logcat.txt'"
adb shell am start -W -n "$ACTIVITY" >/dev/null
check "the next start hands the queued tap to C++ (the day of the tap)" wait_for 20 log_has \
  "SELFTEST done key=selftest-104 local=.* path=strony/Źródła.md line=9 day=$today"
if adb shell run-as "$PKG" true 2>/dev/null; then
  check "… and empties the queue" bash -c "! '$SDK/platform-tools/adb' -s $SERIAL shell run-as $PKG cat shared_prefs/$PKG.android.done.xml 2>/dev/null | grep -q selftest-"
fi
adb logcat -d >"$OUT/selftest-logcat.txt"
grep -E 'SELFTEST|BrulionAndroid:|brulion\.android:|AndroidRuntime:' "$OUT/selftest-logcat.txt" >"$OUT/selftest-summary.txt" || true
check "no crash in the self-test" bash -c "! grep -E 'FATAL EXCEPTION|Fatal signal' '$OUT/selftest-logcat.txt'"

info "Result"
cat "$OUT/selftest-summary.txt"
echo "logs and screenshots: $ROOT/$OUT"
((failures == 0)) || die "$failures check(s) failed"
echo "all checks passed"
