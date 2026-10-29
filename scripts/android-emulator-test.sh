#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# The helpers defined inside a mode run through check/wait_for, which shellcheck cannot follow.
# shellcheck disable=SC2329
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
# by frame on Android 11+ (the window keeps its size), stays there while the journal scrolls; when
# Back hides the keyboard the bottom navigation comes back and the toolbar stays docked right on it
# (the shell's log, UiAutomator's tabs); a tap on the text brings the keyboard back, the toolbar
# on it and the navigation away again;
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
# With --drawing instead: text on a drawing with the on-screen keyboard (docs/UI.md "Drawing
# editor") — a notebook whose journal embeds a drawing (tests/data/drawing/auto_theme.svg): a tap
# on the embed opens the drawing editor, the bar at the bottom has "Tekst" on screen (a phone held
# upright: two lines), a tap on the paper opens the text's editor and the keyboard, words typed on
# the keyboard's keys with a pause longer than the 1.2 s save timer between them (the editor and
# the keyboard stay), the editor's ✓ writes the text — the drawing's file then holds it as a text
# element — and the keyboard goes; a second text started at once after the first one is written
# (while the save timer runs) stays open as well and is written too. Screenshots (dr-*.png); the
# same debuggable APK.
# With --web-keyboard SITE_DIR instead: the web build (scripts/wasm-build.sh -> dist-wasm) in Android's
# browsers — Chrome (in the google_apis image), Brave 1.97.59 and Firefox 157.0.1 (pinned APKs,
# SHA-256 checked, cached in ~/.cache/brulion-web-browsers): the site served from the host under
# /brulion/ (scripts/web-test-server.py; http://10.0.2.2 in the emulator, a secure context), today's
# journal written into IndexedDB, the app opened with ?selftest=keyboard (its results POSTed to the
# server), a block tapped: the keyboard comes up and the editing toolbar sits right on it — on a
# screenshot no magenta row (the app paints the keyboard's room so) between the IME's top and the
# cyan toolbar (gap <= 3 px), and the browser shrank the page (interactive-widget=resizes-content);
# a word typed and Enter reach the block; the phone turned with the keyboard up and back (the toolbar
# at the window's bottom, the keyboard counted as up where it stays: Firefox; Chrome and Brave hide
# it); Back hides the keyboard and leaves the toolbar docked right on the bottom navigation, which
# comes back, nothing magenta. Each browser runs again with
# ?keyboard=visual (the browser's default), for the record (Brave's brave-browser#60074: a gap).
# The app's readings are kept (samples-<browser>-<mode>.jsonl) and printed as rows of
# tests/app/tst_keyboardinsets.cpp; screenshots wk-*.png. docs/WEB.md "Klawiatura ekranowa".
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
#        scripts/android-emulator-test.sh --web-keyboard [SITE_DIR]   (default dist-wasm)
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
#   BRULION_WEB_PORT         --web-keyboard: the host's port of the site (default 8123)
#   BRULION_WEB_BROWSERS     --web-keyboard: the browsers tested (default "chrome brave firefox")
#   BRULION_WEB_BROWSERS_CACHE --web-keyboard: the browsers' APKs (~/.cache/brulion-web-browsers)
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
DRAWING=0
WEBKEYBOARD=0
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
elif [[ ${1:-} == --drawing ]]; then
  DRAWING=1
  shift
elif [[ ${1:-} == --web-pictures ]]; then
  WEBPICTURES=1
  shift
elif [[ ${1:-} == --web-keyboard ]]; then
  WEBKEYBOARD=1
  shift
fi
if ((WEBKEYBOARD)); then
  SITE_DIR=${1:-dist-wasm}
  [[ -f $SITE_DIR/index.html && -f $SITE_DIR/brulion.wasm ]] || die "$SITE_DIR is not the web build (scripts/wasm-build.sh)"
  SITE_DIR=$(cd -- "$SITE_DIR" && pwd)
else
  APP_APK=${1:-dist-android/brulion-$VERSION-x86_64.apk}
  SELFTEST_APK=${2:-dist-android/brulion-selftest-$VERSION-x86_64.apk}
  [[ -f $APP_APK ]] || die "$APP_APK not found (build it: BRULION_ANDROID_ABIS=x86_64 BRULION_ANDROID_SELFTEST=1 scripts/android-build.sh)"
  ((KEYBOARD || SELECTION || FOLDERS || SHARE || WIDGETS || WEBPICTURES || DONE || DRAWING)) || [[ -f $SELFTEST_APK ]] ||
    die "$SELFTEST_APK not found"
fi

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

# The adb server outlives the test: started here with none of the test's open files but the
# standard ones. It inherited a lock its caller held (`flock ~/work/emulator.lock …`) and kept it
# after the test — every later emulator test waited for it.
(
  for fd in /proc/"$BASHPID"/fd/*; do
    fd=${fd##*/}
    if ((fd > 2)); then eval "exec $fd>&-" 2>/dev/null || true; fi
  done
  exec "$SDK/platform-tools/adb" start-server
) </dev/null >/dev/null 2>&1 || true
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
# ends <end> px above the window's bottom (keyboard <k> px, navigation bar <s> px, bottom
# navigation <n> px), editing toolbar docked". With the keyboard hidden by the system the bottom
# navigation is back (60 px over the navigation bar) and the toolbar stays docked right on it: the
# view ends exactly that far above the bottom. With the keyboard up the navigation is away (0) and
# the view ends at the keyboard's top.
typing_report() {
  adb logcat -d | grep -oE "Typing: the content ends [0-9]+ px above the window's bottom \(keyboard [0-9]+ px, navigation bar [0-9]+ px, bottom navigation [0-9]+ px\), editing toolbar (docked|not docked)" |
    tail -n 1
}
toolbar_on_bottom_navigation() {
  local report
  report=$(typing_report)
  [[ $report =~ ends\ ([0-9]+)\ px.*keyboard\ 0\ px,\ navigation\ bar\ ([0-9]+)\ px,\ bottom\ navigation\ ([0-9]+)\ px.*toolbar\ docked$ ]] &&
    ((BASH_REMATCH[3] > BASH_REMATCH[2] && BASH_REMATCH[1] == BASH_REMATCH[3]))
}
toolbar_above_tabs() { # the toolbar's centre above the bottom navigation's Kalendarz (UiAutomator)
  local bar tab
  bar=$(ui_y 'Narzędzia edycji')
  tab=$(ui_y 'Kalendarz')
  echo "    toolbar at y=${bar:-?}, Kalendarz at y=${tab:-?} (px)"
  [[ -n $bar && -n $tab ]] && ((bar < tab))
}
toolbar_on_keyboard_report() {
  local report
  report=$(typing_report)
  [[ $report =~ ends\ ([0-9]+)\ px.*keyboard\ ([0-9]+)\ px,.*bottom\ navigation\ 0\ px.*toolbar\ docked$ ]] &&
    ((BASH_REMATCH[2] > 0 && BASH_REMATCH[1] == BASH_REMATCH[2]))
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
# The bounds "x1 y1 x2 y2" of the nodes whose text or content-desc is the whole ERE, in the last
# dump (ui.xml), top to bottom. Only those on the screen: a row scrolled away or hidden keeps its
# node (bounds 0,0,0,0). Bounds are not clipped to a scrolling list, only to the window: a row
# scrolled under a list's bottom edge is "on screen" there (see drawer_view). Nothing found is no
# failure (an empty list): a failing grep made `xy=$(…)` in ui_tap fail, and `set -e` ended the
# whole script without a word where ui_tap was the last command of an `||` list.
ui_nodes() {
  sed 's/<node /\n<node /g' "$OUT/ui.xml" | { grep -E "(text|content-desc)=\"($1)\"" || true; } |
    sed -nE 's/.*bounds="\[([0-9]+),([0-9]+)\]\[([0-9]+),([0-9]+)\]".*/\1 \2 \3 \4/p' |
    awk -v w="$SCREEN_W" -v h="$SCREEN_H" '$3 > $1 && $4 > $2 && $3 <= w && $4 <= h' | sort -n -k 2
}
# The centres "x y" of the nodes whose text or content-desc is the whole ERE, top to bottom (a new
# dump; as ui_nodes).
ui_find_all() {
  ui_dump
  ui_nodes "$1" | awk '{ print int(($1 + $3) / 2), int(($2 + $4) / 2) }' | sort -n -k 2
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
# The drawer's list on screen, "top bottom" (in the last dump): below the notebook's row (its
# "Przełącz notatnik"), above the sync footer ("Synchronizacja: …"); nothing when the drawer is
# closed. Its rows (and the "Strony" header above them) scroll inside it and keep their nodes
# outside it: a row under the footer is on screen for ui_find_all, and a tap on it lands on the
# footer — the sync center opened instead of the folder the tap was for (sweep2, 2026-10-29).
drawer_view() {
  local top bottom
  top=$(ui_nodes 'Przełącz notatnik' | head -n 1 | cut -d ' ' -f 4)
  bottom=$(ui_nodes 'Synchronizacja: .*' | tail -n 1 | cut -d ' ' -f 2)
  [[ -n $top && -n $bottom ]] && echo "$top $bottom"
}
drawer_in_view() { # <ERE>: the first item so named lies wholly in the drawer's list (a new dump)
  local view top bottom item _ y1 y2
  ui_dump
  view=$(drawer_view) || return 1
  read -r top bottom <<<"$view"
  item=$(ui_nodes "$1" | head -n 1)
  [[ -n $item ]] || return 1
  read -r _ y1 _ y2 <<<"$item"
  ((y1 >= top && y2 <= bottom))
}
drawer_show() { # <ERE>: a finger drags the drawer's list until the item so named lies wholly in it
  # (towards its middle: the slow drag's little fling cannot take it out again). An item not on
  # the screen at all is further down the list. After each drag the drawer is closed and opened
  # again (Back, ☰): on Android 11 the accessibility tree had no nodes for the rows the drag
  # brought into the list and kept others at their old places until the drawer showed again.
  local _ view top bottom item y1 y2 x from to delta
  x=$((SCREEN_W * 30 / 100)) # (on the rows' text, left of their "⋯")
  for _ in 1 2 3 4 5 6 7 8; do
    ui_dump
    if ! view=$(drawer_view); then
      echo "    the drawer is not open"
      return 1
    fi
    read -r top bottom <<<"$view"
    item=$(ui_nodes "$1" | head -n 1)
    if [[ -n $item ]]; then
      read -r _ y1 _ y2 <<<"$item"
      ((y1 >= top && y2 <= bottom)) && return 0
      delta=$(((y1 + y2) / 2 - (top + bottom) / 2))
    else
      y1="" y2=""
      delta=$(((bottom - top) / 2))
    fi
    # Dragged up (delta > 0) from near the footer, down from near the notebook's row, never
    # beyond the list.
    if ((delta > 0)); then from=$((bottom - 30)); else from=$((top + 30)); fi
    to=$((from - delta))
    ((to < top + 30)) && to=$((top + 30))
    ((to > bottom - 30)) && to=$((bottom - 30))
    echo "    (the drawer's list ${top}-$bottom, the item ${y1:-?}-${y2:-?}: dragged $from → $to)"
    adb shell input swipe "$x" "$from" "$x" "$to" 1200
    sleep 1
    adb shell input keyevent 4
    sleep 2
    ui_tap 'Menu' first 1 || return 1
  done
  echo "    not brought into the drawer's list: $1"
  return 1
}
drawer_tap() { drawer_show "$1" && ui_tap "$1" first "${2:-2}"; } # <ERE> [pause]
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
  # Back hides the keyboard: the bottom navigation comes back, the toolbar stays docked right on
  # it while editing goes on.
  adb shell input keyevent 4
  check "Back hides the keyboard" wait_for 10 ime_hidden
  sleep 2
  app_shot keyboard-journal-hidden
  check "the app reports it hidden" wait_for 10 keyboard_reported_hidden
  check "the toolbar stays docked right on the bottom navigation, editing goes on" \
    wait_for 10 toolbar_on_bottom_navigation
  echo "    $(typing_report)"
  check "the bottom navigation's tabs are on screen, the editing toolbar above them" toolbar_above_tabs
  # A tap on the edited text brings the keyboard back: the toolbar on it, the navigation away.
  adb shell input tap $((SCREEN_W * 24 / 100)) $((SCREEN_H * 25 / 100))
  check "a tap on the text brings the keyboard back" wait_for 15 ime_shown
  sleep 3
  app_shot keyboard-journal-again
  check "the app's view ends at the keyboard's top again" keyboard_matches
  check "the toolbar on the keyboard, the bottom navigation away" wait_for 10 toolbar_on_keyboard_report
  echo "    $(typing_report)"
  check "the navigation's tabs are gone" ui_lacks 'Kalendarz'
  # A page: the guide (opened as by a tapped notification), its first paragraph tapped.
  start_app '--demo --locale pl' --es "$EXTRA.PATH" "'Witaj w Brulionie.md'" --ei "$EXTRA.LINE" 0
  app_shot keyboard-page-start
  # Its first paragraph, found by its text (the page's tag chips, "Widok tagu" and the properties
  # above it move it down — a blind tap opened the tag's tasks); else a few heights 25–45 % down
  # the screen (the system bars differ between versions).
  paragraph_xy=$(ui_find_all 'Brulion to notatnik w zwykłych plikach Markdown[^"]*' | head -n 1)
  if [[ -n $paragraph_xy ]]; then
    adb shell input tap "$paragraph_xy"
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

long_press() { # <x> <y>, or one "<x> <y>" word as ui_find_all prints it
  local x y
  read -r x y <<<"$*"
  adb shell input swipe "$x" "$y" "$x" "$y" 1400
}

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
  # Kopiuj in the docked tools, right on the bottom navigation.
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
  # The drawer opened on that page shows its row in the tree ("Pokazuj otwartą stronę": revealed
  # while the drawer was closed, placed as it opens) in the list's view, not under its footer.
  check "☰ opens the drawer" ui_tap 'Menu' first
  check "the drawer shows the page's row in its view" wait_for 6 drawer_in_view 'Więcej: Witaj w Brulionie'
  app_shot fl-2b-drawer-on-page
  adb shell input keyevent 4 # Back: the drawer closes
  sleep 2
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
  # Whatever is tapped in the drawer's list is first brought into its view (drawer_tap).
  check "☰ opens the drawer" ui_tap 'Menu' first
  check "the drawer shows the Strony section" wait_for 10 ui_has 'Strony.*'
  app_shot fl-9-drawer
  check "the Strony header's + opens its menu" drawer_tap 'Nowa strona lub folder'
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
  # The drawer opens again with the new folder revealed: its row in the list's view, above the
  # sync footer (the app placed it while the drawer still had the keyboard's height: the row
  # ended up under the footer, where a tap opened the sync center — sweep2, 2026-10-29).
  check "and shows in the tree, in the drawer's view" wait_for 6 drawer_in_view "$folder(, folder .*)?"
  app_shot fl-11-folder-made

  # "⋯" on a page of the tree → "Przenieś do…": "Remont mieszkania" from Projekty into Archiwum.
  check "the folder Projekty is brought into the drawer's view" drawer_show 'Projekty, folder .*'
  if ui_has 'Projekty, folder zwinięty'; then
    check "a tap opens it" ui_tap 'Projekty, folder zwinięty'
  fi
  check "the page's row has its ⋯ button" drawer_tap 'Więcej: Remont mieszkania'
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
  [[ -z $xy ]] || long_press "$xy"
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
  adb shell input swipe "$xy" "$xy" 1500
  sleep 2
  ui_dump
  handle=$(sed 's/<node /\n<node /g' "$OUT/ui.xml" | grep 'widget_resize_bottom_handle' |
    sed -nE 's/.*bounds="\[([0-9]+),([0-9]+)\]\[([0-9]+),([0-9]+)\]".*/\1 \2 \3 \4/p' | awk '{ print int(($1 + $3) / 2), int(($2 + $4) / 2) }')
  [[ -n $handle ]] || return 1
  adb shell input swipe "$handle" "${handle% *}" $((${handle#* } + $2)) 1500
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
  adb shell am start -n "$QUICK_ADD" >/dev/null 2>&1 ||
    die "cannot start the (not exported) QuickAddActivity: a google_apis image with adb root is needed"
  adb shell am force-stop "$PKG"
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

# --- text on a drawing with the on-screen keyboard (--drawing) ------------------------------------

if ((DRAWING)); then
  info "Text on a drawing: $APP_APK on ${SCREEN_W}x$SCREEN_H, density $DENSITY, API $android_api"
  adb install -r -g "$APP_APK" >/dev/null
  adb shell run-as "$PKG" true 2>/dev/null || die "$APP_APK is not debuggable (BRULION_ANDROID_BUILD_TYPE=Debug)"
  DR_VAULT=files/Rysunki
  today=$(adb shell date +%F | tr -d '\r')
  printf -- '- Szkic z wykładu\n  ![[Rysunek.svg]]\n' >"$OUT/journal.md"
  adb push "$OUT/journal.md" /data/local/tmp/brulion-journal.md >/dev/null
  adb push tests/data/drawing/auto_theme.svg /data/local/tmp/brulion-drawing.svg >/dev/null
  adb shell run-as "$PKG" mkdir -p "$DR_VAULT/journals" "$DR_VAULT/assets"
  adb shell run-as "$PKG" cp /data/local/tmp/brulion-journal.md "$DR_VAULT/journals/$today.md"
  adb shell run-as "$PKG" cp /data/local/tmp/brulion-drawing.svg "$DR_VAULT/assets/Rysunek.svg"
  # The texts of the drawing's text elements (its embedded scene), one a line, in lower case.
  dr_texts() {
    adb shell run-as "$PKG" base64 "$DR_VAULT/assets/Rysunek.svg" 2>/dev/null | tr -d '\r\n' | base64 -d >"$OUT/drawing.svg" || true
    node -e '
      const svg = require("fs").readFileSync(process.argv[1], "latin1");
      const m = /<!-- payload-start -->\s*([^<]*?)\s*<!-- payload-end -->/.exec(svg);
      if (!m) process.exit(0);
      const w = JSON.parse(Buffer.from(m[1], "base64").toString("latin1"));
      const bytes = Buffer.from(w.encoded, "latin1");
      const scene = JSON.parse((w.compressed ? require("zlib").inflateSync(bytes) : bytes).toString("utf8"));
      for (const e of scene.elements) if (e.type === "text") console.log(e.text.toLowerCase());' "$OUT/drawing.svg"
  }
  dr_has() { dr_texts | grep -qxF -- "$1"; } # <the whole text, lower case>
  dr_paper() { adb shell input tap $((SCREEN_W / 2)) $((SCREEN_H * $1 / 100)); } # <% of the height>
  editor_open() { ui_has 'Gotowe' && [[ $(ui_find_all 'Gotowe' | wc -l) -ge 2 ]]; } # (its ✓ and the bar's)
  start_app "--vault /data/user/0/$PKG/$DR_VAULT --locale pl --route journals"
  check "the app is running" running
  app_shot dr-1-journal
  check "a tap on the drawing's embed opens the drawing editor" ui_tap 'Rysunek(\.svg)?' first 4
  check "… its bar has Tekst on screen" wait_for 15 ui_has 'Tekst(, T)?'
  check "… and Więcej and Gotowe" ui_has 'Więcej'
  app_shot dr-2-editor
  ui_tap 'Tekst(, T)?' first 1
  dr_paper 45
  check "a tap on the paper with Tekst opens the text's editor and the keyboard" wait_for 10 ime_shown
  sleep 1
  type_on_keyboard 'Draw the'
  # A pause longer than the save timer (1.2 s) mid-text: the editor and the keyboard stay.
  sleep 3
  check "… the editor stays open while the user pauses" editor_open
  check "… and so does the keyboard" ime_shown
  type_on_keyboard ' plan'
  sleep 1
  app_shot dr-3-typing
  check "its ✓ writes the text" ui_tap 'Gotowe' first 0
  # At once a second text (the save timer runs: the first one was just written), high on the paper
  # (the keyboard is still going: Android shrinks the window under it).
  dr_paper 30
  sleep 3
  check "a second text started at once stays open" editor_open
  check "… with the keyboard" ime_shown
  type_on_keyboard 'Second note'
  ui_tap 'Gotowe' first 1
  check "the keyboard goes once the text is written" wait_for 8 ime_hidden
  check "the drawing's file holds the first text" wait_for 10 dr_has 'draw the plan'
  check "… and the second" dr_has 'second note'
  app_shot dr-4-written
  echo "    the drawing's texts:"
  dr_texts | sed 's/^/      /'

  adb logcat -d >"$OUT/drawing-logcat.txt"
  check "no crash" bash -c "! grep -E 'FATAL EXCEPTION|Fatal signal' '$OUT/drawing-logcat.txt'"
  check "no QML errors" bash -c "! grep -E 'TypeError|ReferenceError|Binding loop detected' '$OUT/drawing-logcat.txt'"
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
    return "$ok"
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
  adb shell input tap "$picture_xy"
  sleep 2
  check "a tap opens the viewer" wait_for 10 ui_has 'Zamknij'
  app_shot wp-4-viewer
  check "… actual size" ui_tap 'Pokaż naturalny rozmiar'
  check "… and fit again" ui_tap 'Dopasuj do widoku'
  adb shell input keyevent 4
  check "Back closes the viewer" wait_for 10 ui_lacks 'Zamknij'
  check "… and stays in the journal" ui_has 'Gradient przez http'
  # 4. A long press opens the picture's menu.
  long_press "$picture_xy"
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
  long_press "$(ui_find_all 'Logo przez https' | tail -n 1)"
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

# --- the web build's editing toolbar on the keyboard in Android's browsers (--web-keyboard) ----------

if ((WEBKEYBOARD)); then
  info "Web keyboard: $SITE_DIR on ${SCREEN_W}x$SCREEN_H, density $DENSITY, API $android_api"
  WK_PORT=${BRULION_WEB_PORT:-8123}
  WK_CACHE=${BRULION_WEB_BROWSERS_CACHE:-$HOME/.cache/brulion-web-browsers}
  # Pinned browsers: Brave of brave-browser#60074 (its visualViewport leaves out the bottom
  # toolbar it hides for the keyboard), a stable Firefox (x86_64 builds); Chrome comes with the
  # google_apis image.
  BRAVE_VERSION=1.97.59
  BRAVE_SHA256=03ba680494a9abd6a5b68fed7bd6f3ebc1b6e06f9d702dcff2a61bb8ae3ec7de
  FENIX_VERSION=157.0.1
  FENIX_SHA256=a6f397305508603cb0548dca8c321c412bc6eda7765f417fd142f61c9918e29c
  # Helpers in Python (standard library): the report's records of a run, the raw screenshot's rows.
  WK_PY=$(
    cat <<'PY'
import json, struct, sys

def records(report, run):
    out = []
    with open(report, encoding="utf-8") as f:
        for line in f:
            try:
                r = json.loads(line)
            except ValueError:
                continue
            body = r.get("body")
            if r.get("path") == "/__report/" + run and isinstance(body, dict) and body.get("name") == "brulionKeyboardSelfTest":
                out.append(body.get("json") or {})
    return out

def num(v, default=0):
    return v if isinstance(v, (int, float)) else default

def state(report, run):
    # "count windowHeight overlap shown animating editing settled toolbarBottom contentBottom legacy
    # containerHeight innerWidth bottomNavTop"
    rs = records(report, run)
    if not rs:
        return
    k = rs[-1]
    s = k.get("sample") or {}
    print(len(rs), round(num(k.get("windowHeight")), 1), round(num(k.get("overlap")), 1), int(bool(k.get("shown"))),
          int(bool(k.get("animating"))), int(bool(s.get("editing"))), int(bool(s.get("settled"))),
          round(num(k.get("toolbarBottom"), -1), 1), round(num(k.get("contentBottom")), 1), round(num(k.get("legacyOverlap")), 1),
          round(num(s.get("containerHeight")), 1), round(num(s.get("innerWidth")), 1),
          round(num(k.get("bottomNavTop"), -1), 1))

def cpp(v):
    if not isinstance(v, (int, float)):
        return "kNaN"
    return ("%.2f" % v).rstrip("0").rstrip(".") if v != int(v) else str(int(v))

def samples(report, run, start, out):
    # The run's samples from record `start` on (and the one before: the page before the tap), as
    # JSON lines and as rows for tests/app/tst_keyboardinsets.cpp (one per change; none before the
    # browser's first reading); the Qt window's heights on the way.
    rs = records(report, run)
    picked = rs[max(0, int(start) - 1):]
    last, rows, heights = None, [], []
    with open(out, "w", encoding="utf-8") as f:
        for k in picked:
            s = k.get("sample") or {}
            key = json.dumps([s, k.get("overlap"), k.get("shown"), k.get("animating")], sort_keys=True)
            wh = num(k.get("windowHeight"))
            if not heights or heights[-1] != wh:
                heights.append(wh)
            if key == last or not s or not k.get("mode"):
                continue
            last = key
            f.write(json.dumps(k, ensure_ascii=False) + "\n")
            mode = k.get("mode") or ""
            row = "step(%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, u\"%s\"_s, %s, %s)," % (
                cpp(s.get("innerWidth")), cpp(s.get("innerHeight")), cpp(s.get("height")), cpp(s.get("containerHeight")),
                cpp(k.get("windowHeight")), str(bool(s.get("editing"))).lower(), str(bool(s.get("settled"))).lower(),
                cpp(k.get("overlap")), str(bool(k.get("shown"))).lower(), str(bool(k.get("animating"))).lower(), mode,
                cpp(s.get("offsetTop")), str(bool(s.get("touch"))).lower())
            if num(s.get("scale"), 1) != 1:
                row += " // scale %s" % s.get("scale")
            rows.append(row)
    print("    Qt's window heights: " + " -> ".join(cpp(h) for h in heights))
    for row in rows:
        print("      " + row)

def screen(raw, top):
    # Rows up from just above `top` (the keyboard's top, or the screen's bottom): the rows that
    # are not the cyan toolbar (the gap: the keyboard's room — magenta — and the browser's own
    # bars), then the toolbar's rows; also the magenta rows anywhere. A row is magenta or cyan
    # when most of it is.
    data = open(raw, "rb").read()
    if len(data) < 16:
        print("-1 -1 -1 -1")
        return
    w, h = struct.unpack_from("<II", data, 0)
    off = len(data) - w * h * 4
    if w <= 0 or h <= 0 or off < 12:
        print("-1 -1 -1 -1")
        return
    def kind(y):
        row = data[off + y * w * 4: off + (y + 1) * w * 4]
        m = c = n = 0
        for x in range(0, w, 3):
            r, g, b = row[x * 4], row[x * 4 + 1], row[x * 4 + 2]
            n += 1
            if r > 200 and g < 70 and b > 200:
                m += 1
            elif r < 70 and g > 200 and b > 200:
                c += 1
        return "m" if m * 2 > n else "c" if c * 2 > n else "."
    kinds = [kind(y) for y in range(h)]
    y = min(int(top), h) - 1
    gap = magenta = 0
    while y >= 0 and kinds[y] != "c" and gap < h:
        magenta += kinds[y] == "m"
        gap += 1
        y -= 1
    rows = 0
    while y >= 0 and kinds[y] == "c":
        rows += 1
        y -= 1
    print(gap if y >= -1 and rows else -1, magenta, rows, kinds.count("m"))

{"state": state, "samples": samples, "screen": screen}[sys.argv[1]](*sys.argv[2:])
PY
  )
  wk_py() { python3 -c "$WK_PY" "$@"; }

  # The site under /brulion/ (as on GitHub Pages) and a page that writes today's journal into
  # IndexedDB the way the app's IDBFS mount stores it (as scripts/wasm-smoke.mjs does), then
  # opens the app with its own query.
  www=$(mktemp -d)
  ln -s "$SITE_DIR" "$www/brulion"
  cat >"$www/seed.html" <<'HTML'
<!doctype html>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Brulion: test journal</title>
<p id="status">Zapisywanie dziennika…</p>
<script>
(async () => {
  const root = '/home/web_user/Brulion';
  const appData = '/home/web_user/.local/share/Brulion/Brulion';
  const enc = new TextEncoder();
  const timestamp = new Date();
  const now = new Date();
  const day = now.getFullYear() + '-' + String(now.getMonth() + 1).padStart(2, '0') + '-' + String(now.getDate()).padStart(2, '0');
  let journal = '';
  for (let i = 1; i <= 40; i++) {
    journal += '- Blok ' + i + ': zażółć gęślą jaźń, źródło łódź ćwiczenia — notatka o spotkaniu i ogrodzie, pomysł na wtorek\n';
  }
  const put = (dbName, rows) => new Promise((resolve, reject) => {
    const req = indexedDB.open(dbName, 21);
    req.onupgradeneeded = () => req.result.createObjectStore('FILE_DATA').createIndex('timestamp', 'timestamp', { unique: false });
    req.onerror = () => reject(req.error);
    req.onsuccess = () => {
      const tx = req.result.transaction(['FILE_DATA'], 'readwrite');
      for (const [key, value] of rows) tx.objectStore('FILE_DATA').put(value, key);
      tx.oncomplete = () => { req.result.close(); resolve(); };
      tx.onerror = () => reject(tx.error);
    };
  });
  await put(root, [
    [root + '/journals', { timestamp, mode: 16877 }],
    [root + '/journals/' + day + '.md', { timestamp, mode: 33188, contents: enc.encode(journal) }],
  ]);
  await put(appData, [[appData + '/settings.ini', { timestamp, mode: 33188, contents: enc.encode('[vaults]\ncurrent=' + root + '\n') }]]);
  if (location.search) location.replace('/brulion/' + location.search);
  else fetch('/__report/first-start', { method: 'POST', body: '{}' }).catch(() => {});
})().catch((e) => { document.getElementById('status').textContent = 'Błąd: ' + e; });
</script>
HTML
  : >"$OUT/report.jsonl"
  python3 "$ROOT/scripts/web-test-server.py" "$www" "$WK_PORT" "$OUT/report.jsonl" >"$OUT/web-server.log" 2>&1 &
  http_pid=$!
  for _ in $(seq 1 20); do
    grep -q listening "$OUT/web-server.log" 2>/dev/null && break
    sleep 0.5
  done
  grep -q listening "$OUT/web-server.log" || die "the web server did not start on port $WK_PORT (BRULION_WEB_PORT): $(cat "$OUT/web-server.log")"
  # The host's server at the emulator's alias of the host's loopback, 10.0.2.2 (not `adb
  # reverse`: adb's connection to the emulator drops now and then and takes the forwarding with
  # it). The browsers treat it as a secure context (the service worker, Web Locks): Chrome and
  # Brave by their command line, Firefox (GeckoView) by its configuration file — both read on a
  # debuggable system image for the debug app.
  WK_ORIGIN=http://10.0.2.2:$WK_PORT

  # The browsers: [name] = package. Chrome and Brave read a command line (their first-run screens
  # off) from /data/local/tmp on a debuggable system image (set-debug-app); anything left (Brave's
  # and Firefox's welcome screens, prompts) is tapped away.
  declare -A WK_PKG=([chrome]=com.android.chrome [brave]=com.brave.browser [firefox]=org.mozilla.firefox)
  wk_apk() { # <file in the cache> <url> <sha256>: a browser's APK, downloaded once
    local file=$WK_CACHE/$1
    mkdir -p "$WK_CACHE"
    if [[ ! -f $file ]]; then
      echo "    downloading $2" >&2
      curl -fsSL --retry 3 -o "$file.part" "$2" && mv "$file.part" "$file"
    fi
    [[ -f $file ]] && sha256sum "$file" | grep -q "^$3 " && echo "$file"
  }
  installed() { adb shell pm list packages "$1" | tr -d '\r' | grep -qx "package:$1"; }
  browsers=()
  for b in ${BRULION_WEB_BROWSERS:-chrome brave firefox}; do
    pkg=${WK_PKG[$b]:-}
    [[ -n $pkg ]] || die "unknown browser $b (BRULION_WEB_BROWSERS: chrome brave firefox)"
    apk=""
    case $b in
      brave) apk=$(wk_apk "BraveMonox64-$BRAVE_VERSION.apk" \
        "https://github.com/brave/brave-browser/releases/download/v$BRAVE_VERSION/BraveMonox64.apk" "$BRAVE_SHA256") ||
        die "Brave $BRAVE_VERSION: download failed or wrong checksum" ;;
      firefox) apk=$(wk_apk "fenix-$FENIX_VERSION.multi.android-x86_64.apk" \
        "https://archive.mozilla.org/pub/fenix/releases/$FENIX_VERSION/android/fenix-$FENIX_VERSION-android-x86_64/fenix-$FENIX_VERSION.multi.android-x86_64.apk" \
        "$FENIX_SHA256") || die "Firefox $FENIX_VERSION: download failed or wrong checksum" ;;
    esac
    if [[ -n $apk ]]; then
      echo "    installing $b ($(basename -- "$apk"))"
      adb install -r -g "$apk" >/dev/null || die "$b did not install"
    fi
    check "$b is installed ($pkg)" installed "$pkg" || continue
    adb shell pm grant "$pkg" android.permission.POST_NOTIFICATIONS >/dev/null 2>&1 || true
    adb shell am set-debug-app --persistent "$pkg" >/dev/null 2>&1 || true
    echo "    $b $(adb shell dumpsys package "$pkg" | tr -d '\r' | sed -nE 's/^ *versionName=//p' | head -n 1)"
    browsers+=("$b")
  done
  for f in chrome-command-line brave-command-line; do
    adb shell "echo '_ --disable-fre --no-default-browser-check --no-first-run --disable-signin-promo --unsafely-treat-insecure-origin-as-secure=$WK_ORIGIN' > /data/local/tmp/$f" || true
  done
  printf 'prefs:\n  dom.securecontext.allowlist: "10.0.2.2"\n' >"$OUT/geckoview-config.yaml"
  adb push "$OUT/geckoview-config.yaml" /data/local/tmp/org.mozilla.firefox-geckoview-config.yaml >/dev/null || true

  # A run's last report: "count windowHeight overlap shown animating editing settled toolbarBottom
  # contentBottom legacyOverlap containerHeight innerWidth" (empty before the app's first one).
  wk_state() { wk_py state "$OUT/report.jsonl" "$1"; }
  # The keyboard counted as up, settled, a block edited and its toolbar docked.
  wk_keyboard_settled() {
    local s
    s=$(wk_state "$1")
    [[ -n $s ]] && awk '{ exit !($4 == 1 && $5 == 0 && $6 == 1 && $7 == 1 && $8 >= 0) }' <<<"$s"
  }
  # The keyboard hidden, settled, the toolbar still docked.
  wk_hidden_settled() {
    local s
    s=$(wk_state "$1")
    [[ -n $s ]] && awk '{ exit !($4 == 0 && $5 == 0 && $7 == 1 && $8 >= 0) }' <<<"$s"
  }
  # The bottom navigation shown and the docked toolbar ending right on its top (±1 px).
  wk_toolbar_on_nav() {
    local s
    s=$(wk_state "$1")
    [[ -n $s ]] && awk '{ d = $8 - $13; if (d < 0) d = -d; exit !($13 > 0 && $8 >= 0 && d <= 1) }' <<<"$s"
  }
  wk_count() { local s; s=$(wk_state "$1"); echo "${s%% *}"; }
  # A page this wide (CSS px) is the turned screen's: 1.3 times the upright screen's width (a page
  # upright with the keyboard up can be wider than high: Brave, 411×409).
  WK_WIDE=$((SCREEN_W * 160 * 13 / (DENSITY * 10)))
  # Settled in landscape, a block edited.
  wk_landscape_settled() {
    local s
    s=$(wk_state "$1")
    [[ -n $s ]] && awk -v wide="$WK_WIDE" '{ exit !($5 == 0 && $6 == 1 && $7 == 1 && $12 >= wide && $2 > 0) }' <<<"$s"
  }
  # Landscape: with the keyboard counted as up the toolbar ends at the window's bottom; with it
  # hidden (a browser that closes it on the turn) the bottom navigation is back and the toolbar
  # docked right on it.
  wk_landscape_toolbar() {
    local s
    s=$(wk_state "$1")
    [[ -n $s ]] && awk '{ d = ($4 == 1 ? $8 - $2 : $8 - $13); if (d < 0) d = -d
      exit !($8 >= 0 && d <= 1 && ($4 == 1 || $13 > 0)) }' <<<"$s"
  }
  # adb's connection to the emulator back (it drops now and then, also while the screen turns).
  wk_adb_ready() {
    local _
    for _ in $(seq 1 30); do
      "$SDK/platform-tools/adb" -s "$SERIAL" shell true >/dev/null 2>&1 && return 0
      sleep 2
      timeout 30 "$SDK/platform-tools/adb" -s "$SERIAL" wait-for-device >/dev/null 2>&1 || true
    done
    return 1
  }
  wk_turn() { # <0 portrait | 1 landscape>: the screen's rotation set (and read back)
    local _ now=""
    for _ in 1 2 3 4 5; do
      wk_adb_ready || true
      adb shell settings put system accelerometer_rotation 0 >/dev/null 2>&1 || true
      adb shell settings put system user_rotation "$1" >/dev/null 2>&1 || true
      sleep 3
      wk_adb_ready || true
      now=$(adb shell settings get system user_rotation 2>/dev/null | tr -d '\r' || true)
      [[ $now == "$1" ]] && return 0
    done
    echo "    the screen did not turn to rotation $1 (user_rotation ${now:-?})"
    return 1
  }
  # The run's page upright (the last report).
  wk_upright() {
    local s
    s=$(wk_state "$1")
    [[ -n $s ]] && awk -v wide="$WK_WIDE" '{ exit !($12 > 0 && $12 < wide) }' <<<"$s"
  }
  # First-run screens, prompts and tips of the browsers: tapped away by their buttons' names
  # (Firefox's "Set as default browser" opens the system's dialog: Cancel).
  WK_DISMISS='Accept & continue|Accept and continue|Use without an account|No thanks|No Thanks|No, thanks|NO THANKS|Not now|NOT NOW|Skip|SKIP|Got it|GOT IT|Maybe later|Not interested|Continue|Close|Dismiss|Cancel|Pomiń|Nie teraz|Nie, dziękuję|Anuluj'
  # A browser's first start: its welcome screens tapped away on a page that does not start the
  # app (seed.html without a query reports itself and stays), then the browser stopped.
  wk_first_start() { # <browser>
    local pkg=${WK_PKG[$1]} i xy last="" quiet=0
    adb shell am start -a android.intent.action.VIEW -d "'$WK_ORIGIN/seed.html'" -p "$pkg" >/dev/null
    for i in $(seq 1 30); do
      sleep 3
      xy=$(ui_find_all "$WK_DISMISS" | head -n 1 || true)
      if [[ -n $xy && $xy != "$last" ]]; then
        echo "    tapping away a first-run screen of $1 at $xy"
        adb shell input tap "$xy"
        last=$xy
        quiet=0
        continue
      fi
      grep -q "\"/__report/first-start\"" "$OUT/report.jsonl" && ((++quiet >= 2)) && break
    done
    app_shot "wk-$1-0-first-start"
    adb shell am force-stop "$pkg"
  }
  # The app up in a page opened after `base` reports of the run (an earlier page's do not count).
  wk_ready_since() { # <run> <base>
    local s
    s=$(wk_state "$1")
    [[ -n $s ]] && awk -v base="$2" '{ exit !($1 > base && $2 > 0) }' <<<"$s"
  }
  wk_open() { # <browser> <run> <query>: the seeded journal, then the app (first-run screens tapped away)
    local pkg=${WK_PKG[$1]} url="$WK_ORIGIN/seed.html?$3" i xy last="" base count seen stall=0 sent=1
    base=$(wk_count "$2")
    base=${base:-0}
    seen=$base
    adb shell am force-stop "$pkg"
    adb shell am start -a android.intent.action.VIEW -d "'$url'" -p "$pkg" >/dev/null
    for i in $(seq 1 60); do
      sleep 3
      wk_ready_since "$2" "$base" && return 0
      xy=$(ui_find_all "$WK_DISMISS" | head -n 1 || true)
      # The same place twice: not a dialog's button (a heading on the browser's home page).
      if [[ -n $xy && $xy != "$last" ]]; then
        echo "    tapping away a browser's screen at $xy"
        adb shell input tap "$xy"
        last=$xy
        stall=0
        continue
      fi
      # Nothing to tap and no report for a minute (the app's tab left behind a welcome screen,
      # or stalled): the browser started again (a tab still loading in the background would keep
      # the app's storage lock).
      count=$(wk_count "$2")
      count=${count:-0}
      if ((count != seen)); then
        seen=$count
        stall=0
      elif ((++stall >= 20 && sent < 3)); then
        echo "    starting $1 again ($i)"
        adb shell am force-stop "$pkg"
        base=$count
        adb shell am start -a android.intent.action.VIEW -d "'$url'" -p "$pkg" >/dev/null
        last=""
        stall=0
        sent=$((sent + 1))
      fi
    done
    wk_ready_since "$2" "$base"
  }
  wk_capture() { # <file>: the screen's raw pixels (header + RGBA), gzipped on the device: 10 MB
    # through `adb exec-out` broke adb's connection to the emulator. Tried again if adb drops out.
    local _
    for _ in 1 2 3; do
      if adb exec-out 'screencap | gzip -1' >"$1.gz" 2>/dev/null && gzip -dc "$1.gz" >"$1" 2>/dev/null && [[ -s $1 ]]; then
        rm -f "$1.gz"
        return 0
      fi
      sleep 2
      adb wait-for-device
    done
    rm -f "$1.gz"
    return 1
  }
  # The keyboard's key whose name matches the ERE: "x y name" (UiAutomator's dump of every window).
  ime_key() {
    adb shell rm -f /sdcard/brulion-ime.xml
    adb shell uiautomator dump --windows /sdcard/brulion-ime.xml >/dev/null 2>&1 || true
    adb exec-out cat /sdcard/brulion-ime.xml 2>/dev/null | sed 's/<node /\n<node /g' | grep 'inputmethod' |
      sed -nE "s/.*content-desc=\"($1)\".*bounds=\"\[([0-9]+),([0-9]+)\]\[([0-9]+),([0-9]+)\]\".*/\2 \3 \4 \5 \1/p" |
      awk '{ print int(($1 + $3) / 2), int(($2 + $4) / 2), $5 }' | head -n 1 || true
  }
  wk_focus_text() { # the focused editor's text in the run's last report
    python3 -c 'import json, sys
last = ""
for line in open(sys.argv[1], encoding="utf-8"):
    try:
        r = json.loads(line)
    except ValueError:
        continue
    b = r.get("body")
    if r.get("path") == "/__report/" + sys.argv[2] and isinstance(b, dict) and isinstance(b.get("json"), dict):
        last = b["json"].get("focusText", last)
print(last)' "$OUT/report.jsonl" "$1"
  }
  wk_focus_has() { local t; t=$(wk_focus_text "$1"); [[ ${t,,} == *"$2"* ]]; } # <run> <lower-case text>
  wk_focus_lacks() { ! wk_focus_has "$@"; }
  wk_ime_top() { # the keyboard's top (ime_top), tried again if adb drops out
    local top _
    for _ in 1 2 3; do
      top=$(ime_top 2>/dev/null || true)
      [[ -n $top ]] && break
      sleep 2
      adb wait-for-device
    done
    echo "$top"
  }
  wk_screen_line() { # <name> <gap> <magenta> <rows> <magenta anywhere>
    echo "    $1: ${2} px between the keyboard's top and the toolbar (${3} px of them the keyboard's room), the toolbar ${4} px tall; magenta rows on screen: $5"
  }

  wk_run() { # <browser> <mode: content|visual>
    local b=$1 mode=$2 run="$1-$2" query start ime gap magenta rows anywhere s legacy
    query="selftest=keyboard&report=/__report/$run"
    [[ $mode == visual ]] && query+="&keyboard=visual"
    info "$b, interactive-widget=resizes-$mode"
    wk_turn 0 || true
    local opened=0
    wk_open "$b" "$run" "$query" || opened=1
    check "$run: the app starts and reports the keyboard" test "$opened" -eq 0
    if ((opened)); then
      app_shot "wk-$run-0-failed"
      ui_dump
      cp "$OUT/ui.xml" "$OUT/wk-$run-0-failed.xml" 2>/dev/null || true
      return
    fi
    if ! wk_upright "$run"; then
      echo "    the page is not upright: the screen turned back"
      wk_turn 0 || true
      wait_for 30 wk_upright "$run" || true
    fi
    sleep 8 # the journal's first frames
    # A browser's prompt over the page (Firefox asks for a survey now and then): tapped away.
    local _ xy
    for _ in 1 2 3; do
      xy=$(ui_find_all "$WK_DISMISS" | head -n 1 || true)
      [[ -n $xy ]] || break
      echo "    tapping away a browser's prompt at $xy"
      adb shell input tap "$xy"
      sleep 3
    done
    app_shot "wk-$run-1-journal"
    start=$(wk_count "$run")
    adb shell input tap $((SCREEN_W / 2)) $((SCREEN_H * 45 / 100))
    check "$run: a tap on a block brings up the keyboard" wait_for 20 ime_shown
    check "$run: the app reports the keyboard up, settled, the block edited, the toolbar docked" \
      wait_for 30 wk_keyboard_settled "$run"
    sleep 2
    ime=$(wk_ime_top)
    wk_capture "$OUT/wk-$run-2-keyboard.raw" || true
    app_shot "wk-$run-2-keyboard"
    s=$(wk_state "$run")
    echo "    report: $s (count windowHeight overlap shown animating editing settled toolbarBottom contentBottom legacyOverlap containerHeight)"
    read -r gap magenta rows anywhere < <(wk_py screen "$OUT/wk-$run-2-keyboard.raw" "${ime:-$SCREEN_H}") ||
      gap=-1 magenta=-1 rows=-1 anywhere=-1
    echo "    the keyboard's top at ${ime:-?} px"
    wk_screen_line "on screen" "$gap" "$magenta" "$rows" "$anywhere"
    legacy=$(awk '{ print $10 }' <<<"$s")
    if [[ $mode == content ]]; then
      # Nothing of the page under the toolbar: no magenta row on screen, the toolbar ends at Qt's
      # window's bottom and the window fills the page. Rows that are neither are the browser's
      # own: Chrome's and Brave's autofill bar (passwords, cards, addresses: 48 and 64 dp) sits
      # over the keyboard for every text field.
      echo "    the browser's own bar between them: $((gap - magenta)) px"
      check "$run: the toolbar sits on the keyboard (no room of the page under it, toolbar >= 20 px, the browser's bar <= 72 dp)" \
        test "$gap" -ge 0 -a "$magenta" -le 3 -a "$rows" -ge 20 -a $((gap - magenta)) -le $((72 * DENSITY / 160))
      check "$run: the toolbar ends at the page's bottom (report)" \
        awk -v s="$s" 'BEGIN { split(s, a, " "); d1 = a[8] - a[2]; d2 = a[2] - a[11];
          exit !(d1 <= 1 && d1 >= -1 && d2 <= 1 && d2 >= -1) }'
      check "$run: the browser shrank the page (overlap 0, the keyboard shown)" \
        awk -v s="$s" 'BEGIN { split(s, a, " "); exit !(a[3] == 0 && a[4] == 1) }'
      echo "    the old formula (innerHeight - height - offsetTop) gives $legacy px"
      # Typing reaches the edited block (the word committed by the space after it), the keyboard's
      # action key is Enter, not Search (Qt's field stays a plain text field, wasm/index.html;
      # docs/WEB.md "Klawiatura ekranowa"), and it splits the block at the caret: the block
      # edited next has the text after the caret, without the word.
      local key
      type_on_keyboard " hello " || true
      check "$run: a word typed on the keyboard reaches the block" wait_for 15 wk_focus_has "$run" hello
      key=$(ime_key 'Enter|Return|Search|Go|Done|Next|Send|Szukaj|Dalej|Gotowe|Wyślij')
      echo "    the keyboard's action key: ${key:-not found}"
      check "$run: the keyboard's action key is Enter (not Search)" test "${key##* }" = Enter
      if [[ -n $key ]]; then
        adb shell input tap "${key% *}"
        check "$run: Enter on the keyboard splits the block" wait_for 15 wk_focus_lacks "$run" hello
      fi
      app_shot "wk-$run-2-typed"
      # The phone turned with the keyboard up, and back: a width never seen whose first heights
      # have the keyboard in them (docs/WEB.md "Klawiatura ekranowa"). Where the keyboard stays
      # up, the app counts it as up and the toolbar ends at the window's bottom; where the browser
      # hides it on the turn (Chrome, Brave), the bottom navigation comes back under the toolbar.
      wk_turn 1 || true
      check "$run: turned with the keyboard up: the app settles in landscape" wait_for 30 wk_landscape_settled "$run"
      sleep 3
      s=$(wk_state "$run")
      app_shot "wk-$run-2-landscape"
      check "$run: landscape: the toolbar at the window's bottom (the keyboard up) or on the bottom navigation" \
        wait_for 10 wk_landscape_toolbar "$run"
      s=$(wk_state "$run")
      # The keyboard up by the input method's state or its visible window (either one may lag).
      if ime_shown || [[ -n $(ime_top 2>/dev/null || true) ]]; then
        echo "    landscape, the keyboard up: $s"
        check "$run: landscape: the keyboard counted as up" awk -v s="$s" 'BEGIN { split(s, a, " "); exit !(a[4] == 1) }'
      else
        echo "    landscape: $b hid the keyboard on the turn, the block still edited (report: $s)"
      fi
      wk_turn 0 || true
      check "$run: turned back upright" wait_for 30 wk_upright "$run"
      sleep 2
      if ime_shown; then
        check "$run: turned back: the keyboard counted as up again" wait_for 30 wk_keyboard_settled "$run"
      else
        echo "    portrait again, the keyboard hidden: tapping the edited block"
        adb shell input tap $((SCREEN_W / 2)) $((SCREEN_H * 30 / 100)) || true
        check "$run: turned back: a tap brings the keyboard up again" wait_for 20 ime_shown
        check "$run: … counted as up" wait_for 30 wk_keyboard_settled "$run"
      fi
      sleep 2
    else
      # resizes-visual, informational: Brave's gap (#60074: the page's room shows) or none.
      if ((magenta > 3)); then
        echo "    resizes-visual: a gap of $magenta px of the page under the toolbar (brave-browser#60074 when this is Brave)"
      else
        echo "    resizes-visual: no gap in $b (if this is Brave: brave-browser#60074 is fixed upstream)"
      fi
    fi
    # Back: the keyboard goes, the block stays edited and its toolbar docked on the bottom
    # navigation, which comes back.
    adb shell input keyevent 4
    check "$run: Back hides the keyboard" wait_for 15 ime_hidden
    check "$run: the app reports the keyboard hidden, the toolbar still docked" wait_for 30 wk_hidden_settled "$run"
    check "$run: the bottom navigation is back, the toolbar docked right on it" wait_for 10 wk_toolbar_on_nav "$run"
    sleep 2
    wk_capture "$OUT/wk-$run-3-back.raw" || true
    app_shot "wk-$run-3-back"
    read -r gap magenta rows anywhere < <(wk_py screen "$OUT/wk-$run-3-back.raw" "$SCREEN_H") ||
      gap=-1 magenta=-1 rows=-1 anywhere=-1
    wk_screen_line "after Back (from the screen's bottom)" "$gap" "$magenta" "$rows" "$anywhere"
    check "$run: no keyboard's room left after Back, the toolbar on screen" test "$anywhere" -eq 0 -a "$rows" -ge 20
    echo "    report: $(wk_state "$run")"
    echo "    samples from the tap to Back (tests/app/tst_keyboardinsets.cpp rows):"
    wk_py samples "$OUT/report.jsonl" "$run" "$start" "$OUT/samples-$run.jsonl"
    rm -f "$OUT/wk-$run-"*.raw
    adb shell am force-stop "${WK_PKG[$b]}"
  }

  for b in "${browsers[@]}"; do
    wk_first_start "$b"
    wk_run "$b" content
    wk_run "$b" visual
  done
  adb reverse --remove-all >/dev/null 2>&1 || true
  info "Result"
  echo "report: $OUT/report.jsonl; samples: $OUT/samples-*.jsonl; screenshots: $OUT/wk-*.png"
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
