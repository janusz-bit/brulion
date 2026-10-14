#!/usr/bin/env bash
# Screenshots of the component gallery (src/app/qml/dev/Gallery.qml): every section ×
# {desktop, phone} × {light, dark} into tests/screenshots/ (git-ignored), headless
# (offscreen platform + software renderer), with a fixed clock so dates are reproducible.
#
#   scripts/screenshots.sh                          # everything (≈ 70 images)
#   scripts/screenshots.sh buttons dialog           # only these sections
#   SIZES=phone THEMES=dark scripts/screenshots.sh reminder
#   LOCALE=en SCALE=2 scripts/screenshots.sh chips  # English UI, 2× device pixel ratio
#   SIZES="desktop-tall phone-tall" scripts/screenshots.sh lists  # whole long sections
#   BRULION_BIN=build-ui/src/app/brulion scripts/screenshots.sh
#
# Files: tests/screenshots/<section>-<size>-<theme>[-<locale>][@<scale>x].png
# Run inside `nix develop` (Qt plugin and QML paths). One app instance at a time (lock).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

if [[ -z "${BRULION_GUI_LOCKED:-}" ]] && command -v flock >/dev/null 2>&1; then
  export BRULION_GUI_LOCKED=1
  exec flock /tmp/brulion-gui.lock "$0" "$@"
fi

bin="${BRULION_BIN:-build/src/app/brulion}"
out="${OUT_DIR:-tests/screenshots}"
locale="${LOCALE:-pl}"
scale="${SCALE:-1}"
now="${NOW:-2026-10-14 10:00}"
delay="${DELAY:-900}"
read -r -a sizes <<< "${SIZES:-desktop phone}"
read -r -a themes <<< "${THEMES:-light dark}"

all_sections=(foundations icons buttons inputs selection chips lists feedback menus dialog confirm
  datepicker reminder diff merge palette switcher split)
if [[ $# -gt 0 ]]; then sections=("$@"); else sections=("${all_sections[@]}"); fi

if [[ ! -x "$bin" ]]; then
  echo "screenshots: $bin not found — build first (scripts/dev-build.sh brulion)" >&2
  exit 1
fi
mkdir -p "$out"

suffix=""
[[ "$locale" != "pl" ]] && suffix+="-$locale"
[[ "$scale" != "1" ]] && suffix+="@${scale}x"

failed=0
for section in "${sections[@]}"; do
  for size in "${sizes[@]}"; do
    case "$size" in
      desktop) geometry=1280x800; extra=() ;;
      phone) geometry=412x915; extra=(--touch) ;;
      tablet) geometry=800x1180; extra=(--touch) ;;
      # Whole sections (content below the fold of the standard sizes); not for popups.
      desktop-tall) geometry=1280x2000; extra=() ;;
      phone-tall) geometry=412x2600; extra=(--touch) ;;
      *) echo "screenshots: unknown size $size" >&2; exit 2 ;;
    esac
    for theme in "${themes[@]}"; do
      file="$out/${section}-${size}-${theme}${suffix}.png"
      if QML_DISABLE_DISK_CACHE=1 QT_FORCE_STDERR_LOGGING=1 QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
        "$bin" --gallery --section "$section" --size "$geometry" --theme "$theme" \
        --locale "$locale" --scale "$scale" --now "$now" --delay "$delay" \
        "${extra[@]}" --screenshot "$file" 2> >(grep -v "^brulion: screenshot" >&2); then
        echo "$file"
      else
        echo "screenshots: FAILED $file" >&2
        failed=1
      fi
    done
  done
done
exit "$failed"
