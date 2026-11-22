#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Checks the store texts and pictures that F-Droid, IzzyOnDroid and Google Play read from
# fastlane/metadata/android (docs/ANDROID.md, "F-Droid and IzzyOnDroid"; docs/GOOGLE_PLAY.md,
# "Store listing") against their limits — the stricter one where the stores differ:
#   * every locale (en-US and pl, or those given with -l) has title.txt (at most 30 characters:
#     Google Play's limit, F-Droid's is 50), short_description.txt (at most 80) and
#     full_description.txt (at most 4000), none of them empty, short_description.txt a single line;
#   * every changelogs/<versionCode>.txt has at most 500 characters, and with -v X.Y.Z each locale
#     has the changelog of that version (versionCode = major*1000000 + minor*1000 + patch, written
#     by scripts/release.sh);
#   * en-US (the fallback of the other locales) has images/icon.png, a PNG of 512 × 512 pixels, and
#     images/featureGraphic.png (Google Play's feature graphic, scripts/play-feature-graphic.sh),
#     a PNG of 1024 × 500 pixels without alpha — so has any other locale that has a feature graphic;
#   * every picture in images/ (also phoneScreenshots/ and the other screenshot folders) is a PNG
#     or a JPEG; Google Play's rules for screenshots: at most 8 in a folder, at least 2 phone
#     screenshots in en-US, and a PNG screenshot has no alpha, sides of 320 to 3840 pixels and its
#     long side at most 2.3 times its short side.
# Characters are counted as UTF-8 characters (not bytes); a final newline does not count. A PNG's
# alpha is its colour type (4 or 6) or a tRNS chunk.
# Needs only coreutils (od, tr, wc).
#
# Usage: scripts/check-fastlane-metadata.sh [-v X.Y.Z] [-l "en-US pl"] [metadata dir]
#   metadata dir  default: fastlane/metadata/android of this repository
# Exit status 0 when everything is fine, 1 with a line per problem on stderr.
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
usage() { awk 'NR > 2 && /^#/ { sub(/^# ?/, ""); print; next } NR > 2 { exit }' "${BASH_SOURCE[0]}"; }

version='' locales='en-US pl'
while getopts 'v:l:h' opt; do
  case $opt in
    v) version=$OPTARG ;;
    l) locales=$OPTARG ;;
    h)
      usage
      exit 0
      ;;
    *)
      usage >&2
      exit 2
      ;;
  esac
done
shift $((OPTIND - 1))
dir=${1:-$ROOT/fastlane/metadata/android}
((${#} <= 1)) || {
  usage >&2
  exit 2
}

problems=0
problem() {
  printf '%s\n' "$*" >&2
  problems=$((problems + 1))
}

# chars <file>: its UTF-8 characters (bytes that do not continue a character), without one final
# newline.
chars() {
  local n
  n=$(LC_ALL=C tr -d '\200-\277' <"$1" | wc -c)
  if [[ -s $1 && $(tail -c 1 -- "$1" | od -An -tx1 | tr -d ' ') == 0a ]]; then n=$((n - 1)); fi
  echo "$n"
}
# text <file> <limit>: present, not empty, at most <limit> characters.
text() {
  local n
  if [[ ! -f $1 ]]; then
    problem "$1: missing"
    return
  fi
  n=$(chars "$1")
  if ((n == 0)); then
    problem "$1: empty"
  elif ((n > $2)); then
    problem "$1: $n characters, at most $2"
  fi
}
# bytes <file> <offset> <count>: those bytes as unsigned decimals, space separated.
bytes() { od -An -tu1 -j "$2" -N "$3" -- "$1" | tr -s ' \n' '  ' | sed 's/^ //; s/ $//'; }

[[ -d $dir ]] || {
  problem "$dir: no such directory"
  exit 1
}

code=''
if [[ -n $version ]]; then
  [[ $version =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]] || {
    echo "not a version X.Y.Z: $version" >&2
    exit 2
  }
  code=$((10#${BASH_REMATCH[1]} * 1000000 + 10#${BASH_REMATCH[2]} * 1000 + 10#${BASH_REMATCH[3]}))
fi

for locale in $locales; do
  d=$dir/$locale
  if [[ ! -d $d ]]; then
    problem "$d: missing"
    continue
  fi
  text "$d/title.txt" 30
  text "$d/short_description.txt" 80
  if [[ -f $d/short_description.txt ]] && (($(tr -cd '\n' <"$d/short_description.txt" | wc -c) > 1)); then
    problem "$d/short_description.txt: more than one line"
  fi
  text "$d/full_description.txt" 4000
  if [[ -n $code && ! -f $d/changelogs/$code.txt ]]; then
    problem "$d/changelogs/$code.txt: missing (the changelog of $version; scripts/release.sh writes it)"
  fi
done

# Every changelog of every locale, also those not listed.
while IFS= read -r -d '' f; do
  name=$(basename -- "$f")
  [[ $name =~ ^[1-9][0-9]*\.txt$ ]] || problem "$f: not named <versionCode>.txt"
  text "$f" 500
done < <(find "$dir" -path '*/changelogs/*' -type f -print0)

# Pictures: PNG (\x89PNG\r\n\x1a\n) or JPEG (\xff\xd8\xff).
png_signature='137 80 78 71 13 10 26 10'
while IFS= read -r -d '' f; do
  case $(bytes "$f" 0 8) in
    "$png_signature" | '255 216 255'*) ;;
    *) problem "$f: neither a PNG nor a JPEG" ;;
  esac
done < <(find "$dir" -path '*/images/*' -type f -print0)

# png_size <file>: "<width> <height>" from the IHDR chunk (32-bit big-endian numbers at bytes 16
# and 20).
png_size() {
  local b
  read -r -a b <<<"$(bytes "$1" 16 8)"
  echo "$(((b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3])) $(((b[4] << 24) | (b[5] << 16) | (b[6] << 8) | b[7]))"
}
# png_alpha <file>: what gives the PNG transparency — its colour type (byte 25: 4 grey + alpha,
# 6 RGBA) or a tRNS chunk before the image data — or nothing.
png_alpha() {
  local type offset=8 size n=0 head
  type=$(bytes "$1" 25 1)
  if [[ $type == 4 || $type == 6 ]]; then
    echo "colour type $type (with alpha)"
    return
  fi
  size=$(wc -c <"$1")
  while ((offset + 8 <= size && n++ < 64)); do
    read -r -a head <<<"$(bytes "$1" "$offset" 8)"
    case "${head[4]} ${head[5]} ${head[6]} ${head[7]}" in
      '116 82 78 83') # tRNS
        echo "a tRNS chunk (transparency)"
        return
        ;;
      '73 68 65 84' | '73 69 78 68') return ;; # IDAT, IEND: no tRNS after them
    esac
    offset=$((offset + 12 + ((head[0] << 24) | (head[1] << 16) | (head[2] << 8) | head[3])))
  done
}
is_png() { [[ $(bytes "$1" 0 8) == "$png_signature" ]]; }

icon=$dir/en-US/images/icon.png
if [[ ! -f $icon ]]; then
  problem "$icon: missing"
elif ! is_png "$icon"; then
  problem "$icon: not a PNG"
else
  size=$(png_size "$icon")
  [[ $size == '512 512' ]] || problem "$icon: ${size/ / × } pixels, not 512 × 512"
fi

# Google Play's feature graphic: "JPEG or 24-bit PNG (no alpha)", 1024 × 500 (a PNG here: the
# script that makes it writes one, and only a PNG's size is read).
for locale in $locales; do
  feature=$dir/$locale/images/featureGraphic.png
  for other in "$dir/$locale"/images/featureGraphic.*; do
    [[ ! -e $other || $other == "$feature" ]] ||
      problem "$other: the feature graphic is featureGraphic.png (scripts/play-feature-graphic.sh)"
  done
  if [[ ! -f $feature ]]; then
    [[ $locale != en-US ]] || problem "$feature: missing (scripts/play-feature-graphic.sh)"
  elif ! is_png "$feature"; then
    problem "$feature: not a PNG"
  else
    size=$(png_size "$feature")
    [[ $size == '1024 500' ]] || problem "$feature: ${size/ / × } pixels, not 1024 × 500"
    alpha=$(png_alpha "$feature")
    [[ -z $alpha ]] || problem "$feature: has alpha, $alpha (Google Play: no alpha)"
  fi
done

# Screenshots (Google Play's limits; F-Droid has none of its own).
while IFS= read -r -d '' folder; do
  count=$(find "$folder" -maxdepth 1 -type f | wc -l)
  ((count <= 8)) || problem "$folder: $count screenshots, at most 8"
  while IFS= read -r -d '' f; do
    is_png "$f" || continue
    read -r width height <<<"$(png_size "$f")"
    short=$((width < height ? width : height)) long=$((width < height ? height : width))
    ((short >= 320 && long <= 3840)) ||
      problem "$f: $width × $height pixels, each side must be 320 to 3840"
    ((long * 10 <= short * 23)) ||
      problem "$f: $width × $height pixels, the long side more than 2.3 times the short one"
    alpha=$(png_alpha "$f")
    [[ -z $alpha ]] || problem "$f: has alpha, $alpha (Google Play: no alpha)"
  done < <(find "$folder" -maxdepth 1 -type f -print0)
done < <(find "$dir" -path '*/images/*Screenshots' -type d -print0)
phone=$dir/en-US/images/phoneScreenshots
if [[ -d $dir/en-US ]] && (($(find "$phone" -maxdepth 1 -type f 2>/dev/null | wc -l) < 2)); then
  problem "$phone: fewer than 2 screenshots (Google Play needs at least 2)"
fi

if ((problems)); then
  echo "$problems problem(s) in $dir" >&2
  exit 1
fi
echo "fastlane metadata OK: $dir"
