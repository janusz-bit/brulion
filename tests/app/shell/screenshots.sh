#!/usr/bin/env bash
# Screenshots of the app shell (the real app, not the component gallery): onboarding, the main
# window with the sample notebook (journals, the guide page and its editor states: page-editing,
# page-suggest, page-reminder, …), settings, the command palette, the
# quick switcher, quick capture and the phone drawer — × {desktop 1280×800, phone 412×915
# --touch} × {light, dark} — into tests/screenshots/shell-<shot>-<size>-<theme>.png.
#
# Every run gets a fresh, temporary XDG config/data home (settings, app data, the sample
# notebook) and the fixed clock --now, so the images are reproducible and nothing of the
# user's own Brulion is touched.
#
#   tests/app/shell/screenshots.sh                    # everything
#   tests/app/shell/screenshots.sh settings palette   # only these shots
#   SIZES=phone THEMES=dark tests/app/shell/screenshots.sh journals
#   SIZES="desktop-tall phone-tall" tests/app/shell/screenshots.sh page   # the whole guide page
#   On the laptop: scripts/remote-build.sh -n shell brulion \
#     --run 'BRULION_BIN=$BRULION_BUILD_DIR/src/app/brulion tests/app/shell/screenshots.sh' \
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
delay="${DELAY:-1400}"
read -r -a sizes <<< "${SIZES:-desktop phone}"
read -r -a themes <<< "${THEMES:-light dark}"

declare -A shot_args=(
  [onboarding]="--onboarding"
  [journals]="--demo --route journals"
  [page]="--demo"
  [settings]="--demo --route settings"
  [settings-editor]="--demo --route {\"view\":\"settings\",\"section\":\"editor\"}"
  [settings-vault]="--demo --route {\"view\":\"settings\",\"section\":\"vault\"}"
  [settings-reminders]="--demo --route {\"view\":\"settings\",\"section\":\"reminders\"}"
  [settings-app]="--demo --route {\"view\":\"settings\",\"section\":\"app\"}"
  [newpage]="--demo --route journals --popup newPage"
  [about]="--demo --route journals --popup about"
  [palette]="--demo --route journals --popup palette"
  [switcher]="--demo --route journals --popup switcher:rem"
  [capture]="--demo --route journals --popup capture"
  [drawer]="--demo --route journals --popup drawer"
  # The block editor in the real app on the guide page (qml/dev/PageScript.qml).
  [page-editing]="--demo --editor-script editing"
  [page-suggest]="--demo --editor-script suggest"
  [page-tags]="--demo --editor-script tags"
  [page-slash]="--demo --editor-script slash"
  [page-date]="--demo --editor-script date"
  [page-reminder]="--demo --editor-script reminder"
  [page-source]="--demo --editor-script source"
  [page-sidepanel]="--demo --editor-script sidepanel"
  [page-empty]="--demo --editor-script empty"
  [page-new]="--demo --route {\"view\":\"page\",\"path\":\"Prezenty.md\"}"
)
all_shots=(onboarding journals page page-editing page-suggest page-tags page-slash page-date page-reminder
  page-source page-sidepanel page-new page-empty settings settings-editor settings-vault settings-reminders settings-app
  palette switcher capture newpage about drawer)
if [[ $# -gt 0 ]]; then shots=("$@"); else shots=("${all_shots[@]}"); fi

if [[ ! -x "$bin" ]]; then
  echo "screenshots: $bin not found — build first" >&2
  exit 1
fi
mkdir -p "$out"

suffix=""
[[ "$locale" != "pl" ]] && suffix+="-$locale"

failed=0
for shot in "${shots[@]}"; do
  [[ -n "${shot_args[$shot]:-}" ]] || { echo "screenshots: unknown shot $shot" >&2; exit 2; }
  read -r -a args <<< "${shot_args[$shot]}"
  # Editor states are scripted after the page loaded (typing, scrolling): give them time.
  shot_delay="$delay"
  [[ "$shot" == page-* ]] && shot_delay=$((delay + 800))
  for size in "${sizes[@]}"; do
    case "$size" in
      desktop) geometry=1280x800; extra=() ;;
      phone) geometry=412x915; extra=(--touch) ;;
      tablet) geometry=800x1180; extra=(--touch) ;;
      # Whole long pages (the guide page: SIZES="desktop-tall phone-tall" … page).
      desktop-tall) geometry=1280x5600; extra=() ;;
      phone-tall) geometry=412x11000; extra=(--touch) ;;
      *) echo "screenshots: unknown size $size" >&2; exit 2 ;;
    esac
    # The drawer only exists on narrow windows.
    [[ "$shot" == drawer && "$size" == desktop* ]] && continue
    # The side panel only exists on wide windows.
    [[ "$shot" == page-sidepanel && "$size" == phone* ]] && continue
    for theme in "${themes[@]}"; do
      file="$out/shell-${shot}-${size}-${theme}${suffix}.png"
      home="$(mktemp -d)"
      if XDG_CONFIG_HOME="$home/config" XDG_DATA_HOME="$home/data" XDG_CACHE_HOME="$home/cache" \
        QML_DISABLE_DISK_CACHE=1 QT_FORCE_STDERR_LOGGING=1 QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
        "$bin" "${args[@]}" --size "$geometry" --theme "$theme" --locale "$locale" --now "$now" \
        --delay "$shot_delay" "${extra[@]}" --screenshot "$file" 2> >(grep -v "^brulion: screenshot" >&2); then
        echo "$file"
      else
        echo "screenshots: FAILED $file" >&2
        failed=1
      fi
      rm -rf "$home"
    done
  done
done
exit "$failed"
