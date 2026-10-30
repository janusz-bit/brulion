#!/usr/bin/env bash
# fonts.sh <env-dir> — the UI benchmark's pinned fonts (docs/UI.md "UI benchmark (Brulion vs
# Obsidian)"): writes <env-dir>/fonts.conf, used by both apps through FONTCONFIG_FILE, with only
# Noto Sans / Serif / Sans Mono (noto-fonts-lgc-plus), Noto Color Emoji and DejaVu (fallback) from
# the repository's nixpkgs, sans-serif → Noto Sans, serif → Noto Serif, monospace → Noto Sans
# Mono, grayscale antialiasing with slight hinting. Prints the font folders, one per line, on
# stdout. Then checks that `fc-match sans-serif` gives Noto Sans (else exit 1).
#
# Environment: UIBENCH_FONT_DIRS — the font folders (space-separated) instead of `nix build`
# (tests, machines without nix).
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
dir="${1:?usage: fonts.sh <env-dir>}"
mkdir -p "$dir/fontcache"

if [[ -n "${UIBENCH_FONT_DIRS:-}" ]]; then
  read -r -a font_dirs <<< "$UIBENCH_FONT_DIRS"
else
  mapfile -t paths < <(nix build --no-link --print-out-paths --inputs-from "path:$repo" \
    nixpkgs#noto-fonts-lgc-plus nixpkgs#noto-fonts-color-emoji nixpkgs#dejavu_fonts)
  font_dirs=()
  for p in "${paths[@]}"; do
    [[ -d "$p/share/fonts" ]] && font_dirs+=("$p/share/fonts")
  done
fi
[[ ${#font_dirs[@]} -gt 0 ]] || { echo "fonts.sh: no font folders" >&2; exit 1; }

{
  echo '<?xml version="1.0"?>'
  echo '<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">'
  echo '<!-- The UI benchmark'"'"'s fonts (tests/app/uibench/fonts.sh): the same for Brulion and Obsidian. -->'
  echo '<fontconfig>'
  for d in "${font_dirs[@]}"; do
    echo "  <dir>$d</dir>"
  done
  echo "  <cachedir>$(cd "$dir" && pwd)/fontcache</cachedir>"
  for pair in "sans-serif:Noto Sans:DejaVu Sans" "serif:Noto Serif:DejaVu Serif" "monospace:Noto Sans Mono:DejaVu Sans Mono"; do
    IFS=: read -r generic first fallback <<< "$pair"
    echo "  <alias binding=\"same\"><family>$generic</family><prefer><family>$first</family><family>Noto Color Emoji</family><family>$fallback</family></prefer></alias>"
  done
  # Applications that name the system UI font.
  for name in system-ui ui-sans-serif; do
    echo "  <alias binding=\"same\"><family>$name</family><prefer><family>Noto Sans</family></prefer></alias>"
  done
  echo '  <match target="font">'
  echo '    <edit name="antialias" mode="assign"><bool>true</bool></edit>'
  echo '    <edit name="hinting" mode="assign"><bool>true</bool></edit>'
  echo '    <edit name="hintstyle" mode="assign"><const>hintslight</const></edit>'
  echo '    <edit name="rgba" mode="assign"><const>none</const></edit>'
  echo '    <edit name="lcdfilter" mode="assign"><const>lcdnone</const></edit>'
  echo '  </match>'
  echo '</fontconfig>'
} > "$dir/fonts.conf"
printf '%s\n' "${font_dirs[@]}"

if [[ -z "${UIBENCH_NO_PREFLIGHT:-}" ]]; then
  got="$(FONTCONFIG_FILE="$dir/fonts.conf" fc-match -f '%{family[0]}' sans-serif)"
  if [[ "$got" != "Noto Sans" ]]; then
    echo "fonts.sh: fc-match sans-serif gives '$got', not Noto Sans" >&2
    exit 1
  fi
fi
