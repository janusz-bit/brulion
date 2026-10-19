#!/usr/bin/env bash
# Screenshots of the planning views in the real app: the calendar (month, week, agenda, the day
# sheet of the week view on phones, the add dialog), the tasks (list, quick add preview, "Show as query") and the
# graph (whole, local, a selected node), with the sample notebook (--demo) and — "big" shots — a
# generated notebook of 2000 pages (make-vault.py) — × {desktop 1280×800, phone 412×915 --touch}
# × {light, dark} into tests/screenshots/planning-<shot>-<size>-<theme>.png.
#
# Every run gets a fresh, temporary XDG home and the fixed clock --now (reproducible images, the
# user's own Brulion untouched).
#
#   tests/app/planning/screenshots.sh                     # everything
#   tests/app/planning/screenshots.sh calendar tasks      # only these shots
#   SIZES=phone THEMES=dark tests/app/planning/screenshots.sh graph
#   On the laptop: scripts/remote-build.sh -n planning brulion \
#     --run 'BRULION_BIN=$BRULION_BUILD_DIR/src/app/brulion tests/app/planning/screenshots.sh' \
#     --pull tests/screenshots
#
# Run inside `nix develop` (Qt plugin and QML paths). One app instance at a time (lock).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.."

if [[ -z "${BRULION_GUI_LOCKED:-}" ]] && command -v flock >/dev/null 2>&1; then
  export BRULION_GUI_LOCKED=1
  exec flock /tmp/brulion-gui.lock "$0" "$@"
fi

bin="${BRULION_BIN:-build/src/app/brulion}"
out="${OUT_DIR:-tests/screenshots}"
locale="${LOCALE:-pl}"
now="${NOW:-2026-10-14 10:00}"
delay="${DELAY:-1600}"
read -r -a sizes <<< "${SIZES:-desktop phone}"
read -r -a themes <<< "${THEMES:-light dark}"

all_shots=(calendar calendar-week calendar-agenda calendar-day calendar-add tasks tasks-add tasks-filtered
  tasks-query graph graph-local graph-selected calendar-big tasks-big graph-big)
if [[ $# -gt 0 ]]; then shots=("$@"); else shots=("${all_shots[@]}"); fi

if [[ ! -x "$bin" ]]; then
  echo "screenshots: $bin not found — build first" >&2
  exit 1
fi
mkdir -p "$out"

# The generated big notebook (only when a "-big" shot is requested).
big_vault=""
for shot in "${shots[@]}"; do
  if [[ "$shot" == *-big ]]; then
    big_vault="$(mktemp -d)/Duży brulion"
    python3 tests/app/planning/make-vault.py "$big_vault" --pages 2000 --today "${now%% *}" >/dev/null
    break
  fi
done

# Arguments of a shot (one per line, so routes may contain spaces).
shot_args() {
  case "$1" in
    calendar) printf '%s\n' --demo --route calendar ;;
    calendar-week) printf '%s\n' --demo --route '{"view":"calendar","mode":"week"}' ;;
    calendar-agenda) printf '%s\n' --demo --route '{"view":"calendar","mode":"agenda"}' ;;
    calendar-day) printf '%s\n' --demo --route '{"view":"calendar","mode":"week","open":"day","date":"2026-10-16"}' ;;
    calendar-add) printf '%s\n' --demo --route '{"view":"calendar","open":"reminder","date":"2026-10-16"}' ;;
    tasks) printf '%s\n' --demo --route tasks ;;
    tasks-add) printf '%s\n' --demo --route '{"view":"tasks","open":"add:Zadzwonić do Ani jutro o 9 #praca"}' ;;
    tasks-filtered) printf '%s\n' --demo --route '{"view":"tasks","query":"path:Projekty/ status:open sort:priority"}' ;;
    tasks-query) printf '%s\n' --demo --route '{"view":"tasks","query":"path:Projekty/ status:open","open":"query"}' ;;
    graph) printf '%s\n' --demo --route graph ;;
    graph-local) printf '%s\n' --demo --route '{"view":"graph","path":"Projekty/Remont mieszkania.md","depth":2}' ;;
    graph-selected) printf '%s\n' --demo --route '{"view":"graph","select":"Projekty/Remont mieszkania.md"}' ;;
    calendar-big) printf '%s\n' --vault "$big_vault" --route calendar ;;
    tasks-big) printf '%s\n' --vault "$big_vault" --route tasks ;;
    graph-big) printf '%s\n' --vault "$big_vault" --route graph ;;
    *) return 1 ;;
  esac
}

suffix=""
[[ "$locale" != "pl" ]] && suffix+="-$locale"

failed=0
for shot in "${shots[@]}"; do
  mapfile -t args < <(shot_args "$shot") || true
  if [[ ${#args[@]} -eq 0 ]]; then
    echo "screenshots: unknown shot $shot" >&2
    exit 2
  fi
  shot_delay="$delay"
  # The graph needs time to settle (big ones more), dialogs a moment to open.
  [[ "$shot" == graph* ]] && shot_delay=$((delay + 2600))
  [[ "$shot" == *-big ]] && shot_delay=$((delay + 4500))
  for size in "${sizes[@]}"; do
    case "$size" in
      desktop) geometry=1280x800; extra=() ;;
      phone) geometry=412x915; extra=(--touch) ;;
      tablet) geometry=800x1180; extra=(--touch) ;;
      *) echo "screenshots: unknown size $size" >&2; exit 2 ;;
    esac
    # The day sheet only exists on narrow windows.
    [[ "$shot" == calendar-day && "$size" == desktop ]] && continue
    for theme in "${themes[@]}"; do
      file="$out/planning-${shot}-${size}-${theme}${suffix}.png"
      home="$(mktemp -d)"
      if XDG_CONFIG_HOME="$home/config" XDG_DATA_HOME="$home/data" XDG_CACHE_HOME="$home/cache" XDG_STATE_HOME="$home/state" \
        QML_DISABLE_DISK_CACHE=1 QT_FORCE_STDERR_LOGGING=1 QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
        "$bin" "${args[@]}" --no-single-instance --size "$geometry" --theme "$theme" --locale "$locale" \
        --now "$now" --delay "$shot_delay" "${extra[@]}" --screenshot "$file" \
        2> >(grep -v "^brulion: screenshot" >&2); then
        echo "$file"
      else
        echo "screenshots: FAILED $file" >&2
        failed=1
      fi
      rm -rf "$home"
    done
  done
done
[[ -n "$big_vault" ]] && rm -rf "$(dirname "$big_vault")"
exit "$failed"
