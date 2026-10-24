#!/usr/bin/env bash
# Screenshots of the block editor playground (`brulion --editor`, qml/dev/EditorPlayground.qml):
# scripted states × {desktop 1280×800, phone 412×915 with --touch} × {light, dark} into
# tests/screenshots/editor-<state>-<size>-<theme>.png, headless, with a fixed clock.
#
#   tests/app/editor/editor-screenshots.sh                  # every state
#   tests/app/editor/editor-screenshots.sh editing slash    # only these
#   SIZES=phone THEMES=dark tests/app/editor/editor-screenshots.sh date
#   BRULION_BIN=$BRULION_BUILD_DIR/src/app/brulion tests/app/editor/editor-screenshots.sh
#
# On the laptop: scripts/remote-build.sh -n editor brulion --run 'BRULION_BIN=$BRULION_BUILD_DIR/src/app/brulion
#   tests/app/editor/editor-screenshots.sh' --pull tests/screenshots
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.."

if [[ -z "${BRULION_GUI_LOCKED:-}" ]] && command -v flock >/dev/null 2>&1; then
  export BRULION_GUI_LOCKED=1
  exec flock /tmp/brulion-gui.lock "$0" "$@"
fi

bin="${BRULION_BIN:-build/src/app/brulion}"
out="${OUT_DIR:-tests/screenshots}"
locale="${LOCALE:-pl}"
now="${NOW:-2026-10-15 10:00}"
delay="${DELAY:-1600}"
read -r -a sizes <<< "${SIZES:-desktop phone}"
read -r -a themes <<< "${THEMES:-light dark}"
all_states=(page editing suggest tags tag-pages slash date zoom folded selected source reminder recurrence table calc query embeds title empty conflict markup
  task-menu task-menu-query task-slash copylink copylink-document link-flash
  selection selection-picked selection-document selection-menu text-menu
  document document-paragraph document-list document-heading document-task document-outline)
if [[ $# -gt 0 ]]; then states=("$@"); else states=("${all_states[@]}"); fi

mkdir -p "$out"
failed=0
for state in "${states[@]}"; do
  script="$state"; [[ "$state" == "page" ]] && script=""
  for size in "${sizes[@]}"; do
    case "$size" in
      desktop) geometry=1280x800; extra=() ;;
      phone) geometry=412x915; extra=(--touch) ;;
      desktop-tall) geometry=1280x2400; extra=() ;;
      phone-tall) geometry=412x3000; extra=(--touch) ;;
      *) echo "editor-screenshots: unknown size $size" >&2; exit 2 ;;
    esac
    for theme in "${themes[@]}"; do
      file="$out/editor-${state}-${size}-${theme}.png"
      args=(--editor --size "$geometry" --theme "$theme" --locale "$locale" --now "$now" --delay "$delay" "${extra[@]}" --screenshot "$file")
      [[ -n "$script" ]] && args+=(--editor-script "$script")
      if QML_DISABLE_DISK_CACHE=1 QT_FORCE_STDERR_LOGGING=1 QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
        "$bin" "${args[@]}" 2> >(grep -v "^brulion: screenshot" >&2); then
        echo "$file"
      else
        echo "editor-screenshots: FAILED $file" >&2
        failed=1
      fi
    done
  done
done
exit "$failed"
