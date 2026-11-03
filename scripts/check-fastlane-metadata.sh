#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Checks the store texts and pictures that F-Droid and IzzyOnDroid read from
# fastlane/metadata/android (docs/ANDROID.md, "F-Droid i IzzyOnDroid") against their limits:
#   * every locale (en-US and pl, or those given with -l) has title.txt (at most 50 characters),
#     short_description.txt (at most 80) and full_description.txt (at most 4000), none of them
#     empty, short_description.txt a single line;
#   * every changelogs/<versionCode>.txt has at most 500 characters, and with -v X.Y.Z each locale
#     has the changelog of that version (versionCode = major*1000000 + minor*1000 + patch, written
#     by scripts/release.sh);
#   * en-US (the fallback of the other locales) has images/icon.png, a PNG of 512 × 512 pixels;
#     every picture in images/ (also phoneScreenshots/ and the other screenshot folders) is a PNG
#     or a JPEG.
# Characters are counted as UTF-8 characters (not bytes); a final newline does not count.
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
  text "$d/title.txt" 50
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

icon=$dir/en-US/images/icon.png
if [[ ! -f $icon ]]; then
  problem "$icon: missing"
elif [[ $(bytes "$icon" 0 8) != "$png_signature" ]]; then
  problem "$icon: not a PNG"
else
  # The IHDR chunk: width and height as 32-bit big-endian numbers at bytes 16 and 20.
  read -r -a size <<<"$(bytes "$icon" 16 8)"
  width=$(((size[0] << 24) | (size[1] << 16) | (size[2] << 8) | size[3]))
  height=$(((size[4] << 24) | (size[5] << 16) | (size[6] << 8) | size[7]))
  ((width == 512 && height == 512)) || problem "$icon: $width × $height pixels, not 512 × 512"
fi

if ((problems)); then
  echo "$problems problem(s) in $dir" >&2
  exit 1
fi
echo "fastlane metadata OK: $dir"
