#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Renders Google Play's feature graphic (docs/GOOGLE_PLAY.md "Store listing"): a 1024 × 500 PNG
# without alpha for each locale, fastlane/metadata/android/<locale>/images/featureGraphic.png, from
# packaging/google-play/feature-graphic.svg with the app's logo (assets/logo.svg) and the locale's
# tagline below. Reproducible: the text is set in the fonts of assets/fonts/ui only (a fontconfig of
# its own, no system fonts), librsvg and optipng come from flake.lock's nixpkgs. Re-run it after a
# change of the logo, the SVG or a tagline; scripts/check-fastlane-metadata.sh checks the result.
#
# Usage: scripts/play-feature-graphic.sh [-o METADATA_DIR] [-l "en-US pl"]
#   -o METADATA_DIR  where the locales' folders are [fastlane/metadata/android of this repository]
#   -l LOCALES       only these locales [en-US pl]
# Needs rsvg-convert (librsvg) and optipng; without them (and with nix) it re-runs itself in
#   nix shell --inputs-from <repository> nixpkgs#librsvg nixpkgs#optipng
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
SCRIPT=$ROOT/scripts/$(basename -- "${BASH_SOURCE[0]}")
die() {
  printf 'play-feature-graphic: error: %s\n' "$*" >&2
  exit 1
}

# The tagline of each locale, two lines under the name (the store listing's language; keep them
# short: the lines start at x = 446 of 1024 and are set at 36 px).
declare -A LINE1=([en-US]='Markdown notes' [pl]='Notatki w Markdown,')
declare -A LINE2=([en-US]='that work offline' [pl]='które działają offline')

out=$ROOT/fastlane/metadata/android locales='en-US pl'
while getopts 'o:l:h' opt; do
  case $opt in
    o) out=$OPTARG ;;
    l) locales=$OPTARG ;;
    h)
      awk 'NR > 2 && /^#/ { sub(/^# ?/, ""); print; next } NR > 2 { exit }' "$SCRIPT"
      exit 0
      ;;
    *) exit 2 ;;
  esac
done
shift $((OPTIND - 1))
(($# == 0)) || die "unexpected arguments: $*"

if ! command -v rsvg-convert >/dev/null || ! command -v optipng >/dev/null; then
  [[ -z ${BRULION_FEATURE_GRAPHIC_NIX:-} ]] || die "rsvg-convert or optipng missing in the nix shell"
  command -v nix >/dev/null || die "needs rsvg-convert (librsvg) and optipng, or nix"
  export BRULION_FEATURE_GRAPHIC_NIX=1
  exec nix shell --inputs-from "$ROOT" nixpkgs#librsvg nixpkgs#optipng --command bash "$SCRIPT" \
    -o "$out" -l "$locales"
fi

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
# The bundled fonts only: the picture is the same on every machine.
mkdir -p "$work/fontcache"
cat >"$work/fonts.conf" <<EOF
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<fontconfig>
  <dir>$ROOT/assets/fonts/ui</dir>
  <cachedir>$work/fontcache</cachedir>
</fontconfig>
EOF
export FONTCONFIG_FILE=$work/fonts.conf
# librsvg loads an <image> only from the SVG's own folder (or below it).
cp -- "$ROOT/assets/logo.svg" "$work/logo.svg"

xml() { sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g' <<<"$1"; }

for locale in $locales; do
  [[ -n ${LINE1[$locale]:-} ]] || die "no tagline for the locale $locale (LINE1/LINE2 in $SCRIPT)"
  [[ -d $out/$locale ]] || die "$out/$locale: no such directory"
  line1=$(xml "${LINE1[$locale]}")
  line2=$(xml "${LINE2[$locale]}")
  # | cannot appear in the taglines' XML (sed's delimiter below).
  [[ $line1$line2 != *'|'* ]] || die "a tagline of $locale has a '|'"
  sed -e "s|@LINE1@|$line1|" -e "s|@LINE2@|$line2|" \
    "$ROOT/packaging/google-play/feature-graphic.svg" >"$work/feature-$locale.svg"
  png=$work/feature-$locale.png
  # The SVG paints every pixel; the background colour only makes sure nothing stays transparent.
  rsvg-convert --width 1024 --height 500 --background-color '#1c2130' \
    -o "$png" "$work/feature-$locale.svg"
  # -np: no palette, so that the opaque RGBA becomes RGB (Play: "JPEG or 24-bit PNG (no alpha)").
  optipng -quiet -o5 -np -strip all "$png"
  # The IHDR chunk's colour type (byte 25): 2 is RGB.
  type=$(od -An -tu1 -j 25 -N 1 -- "$png" | tr -d ' ')
  [[ $type == 2 ]] || die "$png: PNG colour type $type, not 2 (RGB without alpha)"
  mkdir -p "$out/$locale/images"
  cp -- "$png" "$out/$locale/images/featureGraphic.png"
  echo "$out/$locale/images/featureGraphic.png ($(wc -c <"$png") bytes)"
done
