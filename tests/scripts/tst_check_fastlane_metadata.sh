#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Tests scripts/check-fastlane-metadata.sh on small metadata directories — the limits of F-Droid,
# IzzyOnDroid and Google Play it has to enforce, the Polish text at them it has to let through,
# Google Play's feature graphic and screenshots — and then
# runs it over the repository's fastlane/metadata/android, which must have the changelog of the
# version in CMakeLists.txt.
#
# Usage: tests/scripts/tst_check_fastlane_metadata.sh [path/to/check-fastlane-metadata.sh [metadata dir [CMakeLists.txt]]]
#        (default: scripts/check-fastlane-metadata.sh, fastlane/metadata/android, CMakeLists.txt)
set -euo pipefail

script=$(realpath -- "${1:-$(dirname -- "${BASH_SOURCE[0]}")/../../scripts/check-fastlane-metadata.sh}")
metadata=$(realpath -- "${2:-$(dirname -- "$script")/../fastlane/metadata/android}")
cmake=$(realpath -- "${3:-$(dirname -- "$script")/../CMakeLists.txt}")
[[ -f $script && -d $metadata && -f $cmake ]] || {
  echo "no $script, $metadata or $cmake" >&2
  exit 2
}
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT

failed=0
pass() { printf 'PASS   : %s\n' "$1"; }
fail() {
  printf 'FAIL!  : %s\n' "$1"
  failed=$((failed + 1))
}
expect() { # <description> <command...>: the command succeeds
  local what=$1
  shift
  if "$@"; then pass "$what"; else fail "$what"; fi
}
# check <arguments...>: runs the script; sets $status and $out (stdout + stderr).
check() {
  status=0
  out=$(bash "$script" "$@" 2>&1) || status=$?
  printf '%s\n' "$out" | sed 's/^/        | /'
}
has() { grep -qF -- "$1" <<<"$out"; }
problems() { grep -c '^/.*: ' <<<"$out" || true; } # the lines about a file
repeat() { # <text> <count>: the text that many times, without a newline
  local i
  for ((i = 0; i < $2; i++)); do printf '%s' "$1"; done
}

be32() { # <number>: its 4 bytes, big-endian
  local b
  for b in $(($1 >> 24 & 255)) $(($1 >> 16 & 255)) $(($1 >> 8 & 255)) $(($1 & 255)); do
    printf '%b' "\\0$(printf %03o "$b")"
  done
}
# png <file> <width> <height> [colour type [tRNS]]: a PNG's signature and IHDR chunk of that size
# (all the script reads), RGBA (6) by default; with "tRNS" a PLTE and a tRNS chunk follow, then
# IDAT (the chunks without their CRCs, which the script skips without reading).
png() {
  {
    printf '\x89PNG\r\n\x1a\n\0\0\0\x0dIHDR'
    be32 "$2"
    be32 "$3"
    printf '\x08%b\0\0\0' "\\x0${4:-6}" # bit depth, colour type, compression, filter, interlace
    printf 'CRC.'
    if [[ ${5:-} == tRNS ]]; then
      printf '\0\0\0\x06PLTE\xff\0\0\0\xff\0CRC.\0\0\0\x02tRNS\0\xffCRC.'
    fi
    printf '\0\0\0\x01IDAT\x78CRC.'
  } >"$1"
}
# good <dir>: metadata within every limit, its Polish texts exactly at them (multi-byte letters
# count as one character, a final newline as none).
good() {
  local d
  for locale in en-US pl; do
    d=$1/$locale
    mkdir -p "$d/changelogs" "$d/images/phoneScreenshots"
    printf '%s\n' "$(repeat ż 30)" >"$d/title.txt"
    printf '%s\n' "$(repeat ą 80)" >"$d/short_description.txt"
    printf '%s\n' "$(repeat 'Zażółć gęślą jaźń. ' 200)$(repeat ę 200)" >"$d/full_description.txt"
    printf '%s\n' "$(repeat ł 500)" >"$d/changelogs/8001.txt"
  done
  png "$1/en-US/images/icon.png" 512 512
  png "$1/en-US/images/featureGraphic.png" 1024 500 2
  png "$1/pl/images/featureGraphic.png" 1024 500 3
  png "$1/en-US/images/phoneScreenshots/1_dziennik.png" 412 915 2
  png "$1/en-US/images/phoneScreenshots/3_paleta.png" 320 736 3
  printf '\xff\xd8\xff\xe0' >"$1/en-US/images/phoneScreenshots/2_zdjęcie.jpg"
}

good "$tmp/good"
check -v 0.8.1 "$tmp/good"
expect "texts at the limits pass (characters, not bytes)" test "$status/$(problems)" = 0/0
check -v 0.8.2 "$tmp/good"
expect "a missing changelog of the version is reported for each locale" test "$status/$(problems)" = 1/2
expect "... with its versionCode" has "pl/changelogs/8002.txt: missing (the changelog of 0.8.2"

good "$tmp/long"
printf 'ż%s' "$(repeat ż 30)" >"$tmp/long/en-US/title.txt"
printf '%s\n' "$(repeat a 81)" >"$tmp/long/pl/short_description.txt"
printf '%s\n' "$(repeat ę 4001)" >"$tmp/long/pl/full_description.txt"
printf '%s\n\n' "$(repeat ł 500)" >"$tmp/long/en-US/changelogs/8001.txt"
check "$tmp/long"
expect "texts over the limits fail" test "$status" = 1
expect "... the title (Google Play's limit)" has "en-US/title.txt: 31 characters, at most 30"
expect "... the short description" has "pl/short_description.txt: 81 characters, at most 80"
expect "... the full description" has "pl/full_description.txt: 4001 characters, at most 4000"
expect "... a changelog (only one final newline is free)" has "en-US/changelogs/8001.txt: 501 characters, at most 500"
expect "... each of them" has "4 problem(s)"

good "$tmp/broken"
rm -- "$tmp/broken/pl/full_description.txt"
: >"$tmp/broken/en-US/title.txt"
printf 'Dwa\nwiersze\n' >"$tmp/broken/en-US/short_description.txt"
printf 'krótko\n' >"$tmp/broken/pl/changelogs/0.8.1.txt"
png "$tmp/broken/en-US/images/icon.png" 512 256
printf 'GIF89a' >"$tmp/broken/en-US/images/phoneScreenshots/3_animacja.gif"
check "$tmp/broken"
expect "missing, empty and malformed files fail" test "$status" = 1
expect "... a missing description" has "pl/full_description.txt: missing"
expect "... an empty title" has "en-US/title.txt: empty"
expect "... a short description of two lines" has "en-US/short_description.txt: more than one line"
expect "... a changelog not named by its versionCode" has "pl/changelogs/0.8.1.txt: not named <versionCode>.txt"
expect "... an icon of the wrong size" has "en-US/images/icon.png: 512 × 256 pixels, not 512 × 512"
expect "... a picture that is neither PNG nor JPEG" has "3_animacja.gif: neither a PNG nor a JPEG"
expect "... each of them" has "6 problem(s)"

good "$tmp/noicon"
rm -- "$tmp/noicon/en-US/images/icon.png"
rm -r -- "$tmp/noicon/pl"
check "$tmp/noicon"
expect "a missing locale and icon fail" test "$status/$(problems)" = 1/2
expect "... the locale" has "/noicon/pl: missing"
expect "... the icon" has "en-US/images/icon.png: missing"
check -l en-US "$tmp/noicon"
expect "-l: only the locales given are required" has "1 problem(s)"

good "$tmp/play"
png "$tmp/play/en-US/images/featureGraphic.png" 1024 500 6
png "$tmp/play/pl/images/featureGraphic.png" 1024 512 3 tRNS
printf '\xff\xd8\xff\xe0' >"$tmp/play/pl/images/featureGraphic.jpg"
png "$tmp/play/en-US/images/phoneScreenshots/1_dziennik.png" 412 950 2
png "$tmp/play/en-US/images/phoneScreenshots/3_paleta.png" 300 600 3 tRNS
mkdir -p "$tmp/play/pl/images/tenInchScreenshots"
for i in 1 2 3 4 5 6 7 8 9; do png "$tmp/play/pl/images/tenInchScreenshots/$i.png" 1600 2560 0; done
check "$tmp/play"
expect "Google Play's pictures out of its rules fail" test "$status" = 1
expect "... a feature graphic with alpha" has "en-US/images/featureGraphic.png: has alpha, colour type 6"
expect "... a feature graphic of the wrong size" has "pl/images/featureGraphic.png: 1024 × 512 pixels, not 1024 × 500"
expect "... a feature graphic with a tRNS chunk" has "pl/images/featureGraphic.png: has alpha, a tRNS chunk"
expect "... a feature graphic not in featureGraphic.png" has "pl/images/featureGraphic.jpg: the feature graphic is featureGraphic.png"
expect "... a screenshot longer than 2.3 times its width" has "1_dziennik.png: 412 × 950 pixels, the long side more than 2.3"
expect "... a screenshot too small" has "3_paleta.png: 300 × 600 pixels, each side must be 320 to 3840"
expect "... a screenshot with transparency" has "3_paleta.png: has alpha, a tRNS chunk"
expect "... more than 8 screenshots in a folder" has "tenInchScreenshots: 9 screenshots, at most 8"
expect "... each of them" has "8 problem(s)"

good "$tmp/few"
rm -- "$tmp/few/en-US/images/featureGraphic.png" "$tmp/few/en-US/images/phoneScreenshots/"[13]_*.png
rm -- "$tmp/few/pl/images/featureGraphic.png"
check "$tmp/few"
expect "a missing feature graphic and too few phone screenshots fail" test "$status/$(problems)" = 1/2
expect "... the feature graphic of en-US (another locale may have none)" has "en-US/images/featureGraphic.png: missing"
expect "... the screenshots" has "phoneScreenshots: fewer than 2 screenshots"

check -v 1.2 "$tmp/good"
expect "a malformed version is a usage error" test "$status" = 2
check "$tmp/nowhere"
expect "a missing directory fails" test "$status" = 1

version=$(sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+)[[:space:]]*$/\1/p' "$cmake" | head -n 1)
check -v "$version" "$metadata"
expect "the repository's metadata passes, with the changelog of $version" test "$status" = 0

echo
if ((failed)); then
  echo "Totals: $failed failed"
  exit 1
fi
echo "Totals: all passed"
