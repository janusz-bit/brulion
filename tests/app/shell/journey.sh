#!/usr/bin/env bash
# The first-time user journey through the real app (src/app/shell/shelljourney.h), headless,
# for {desktop 1280×800, phone 412×915 --touch} × {light, dark}: a screenshot per step into
# tests/screenshots/journey/<size>-<theme>/NN-<step>.png, each run with a fresh, temporary
# XDG config/data home (onboarding → the sample notebook → writing in today's journal →
# calendar → tasks → a page through a [[link]] → search → graph → sync → import/export →
# history → settings) and the fixed clock --now.
#
#   tests/app/shell/journey.sh                      # all four runs
#   SIZES=phone THEMES=dark tests/app/shell/journey.sh
#   JOURNEY=empty tests/app/shell/journey.sh        # a new, empty notebook instead
#   On the laptop: scripts/remote-build.sh -n qa brulion \
#     --run 'BRULION_BIN=$BRULION_BUILD_DIR/src/app/brulion tests/app/shell/journey.sh' \
#     --pull tests/screenshots
#
# Exit code: 0 when every run finished without failed steps (see "journey: FAIL" lines).
# Run inside `nix develop` (Qt plugin and QML paths). One app instance at a time (lock).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.."

if [[ -z "${BRULION_GUI_LOCKED:-}" ]] && command -v flock >/dev/null 2>&1; then
  export BRULION_GUI_LOCKED=1
  exec flock /tmp/brulion-gui.lock "$0" "$@"
fi

bin="${BRULION_BIN:-build/src/app/brulion}"
out="${OUT_DIR:-tests/screenshots/journey}"
locale="${LOCALE:-pl}"
now="${NOW:-2026-10-14 10:00}"
read -r -a sizes <<< "${SIZES:-desktop phone}"
read -r -a themes <<< "${THEMES:-light dark}"
# JOURNEY=empty: a new, empty notebook instead (every view before the first note).
scenario="${JOURNEY:-sample}"
scenario_args=()
[[ "$scenario" == empty ]] && scenario_args=(--journey-empty)

if [[ ! -x "$bin" ]]; then
  echo "journey: $bin not found — build first" >&2
  exit 1
fi

failed=0
for size in "${sizes[@]}"; do
  case "$size" in
    desktop) geometry=1280x800; extra=() ;;
    phone) geometry=412x915; extra=(--touch) ;;
    tablet) geometry=800x1180; extra=(--touch) ;;
    *) echo "journey: unknown size $size" >&2; exit 2 ;;
  esac
  for theme in "${themes[@]}"; do
    dir="$out/$size-$theme"
    [[ "$scenario" == empty ]] && dir="$out/empty-$size-$theme"
    rm -rf "$dir"
    mkdir -p "$dir"
    home="$(mktemp -d)"
    if XDG_CONFIG_HOME="$home/config" XDG_DATA_HOME="$home/data" XDG_CACHE_HOME="$home/cache" HOME="$home" \
      QML_DISABLE_DISK_CACHE=1 QT_FORCE_STDERR_LOGGING=1 QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
      timeout 300 "$bin" --journey "$dir" "${scenario_args[@]}" --no-single-instance --size "$geometry" --journey-theme "$theme" \
      --locale "$locale" --now "$now" "${extra[@]}" 2> "$dir/log.txt"; then
      echo "journey: $dir OK"
    else
      echo "journey: $dir FAILED (exit $?)" >&2
      failed=1
    fi
    grep -E "journey: (FAIL|FALLBACK)|warning|Error|error" "$dir/log.txt" | grep -v "^journey: (shot|click)" | head -40 || true
    rm -rf "$home"
  done
done
exit "$failed"
