#!/usr/bin/env bash
# Scroll measurements of the real app (src/app/ui/scrollprobe.h): each case opens a view, the
# probe scrolls it down to the end and back up with synthetic input and records every frame —
# frame intervals, GUI work, contentY / originY / contentHeight, delegates created, and jumps
# (content moving on screen more than the input explains). Per run a CSV (one row per frame) and
# a log with the summary go to $OUT_DIR/<vault>-<case>-<size>-<script>.{csv,log}.
#
#   tests/app/perf/scroll-probe.sh                         # the sample notebook, all cases
#   VAULT=big tests/app/perf/scroll-probe.sh journals      # 365 synthetic days (make-vault.py)
#   SIZES=phone SCRIPTS="drag flick" tests/app/perf/scroll-probe.sh journals page-doc
#   On the laptop (no display: Xvfb + Mesa llvmpipe, a real GL path):
#     scripts/remote-build.sh -n perf brulion --run 'nix shell nixpkgs#xvfb-run -c \
#       env BRULION_BIN=$BRULION_BUILD_DIR/src/app/brulion tests/app/perf/scroll-probe.sh' \
#       --pull tests/screenshots/perf
#
# Environment: VAULT demo | big; CASES or arguments: journals page-doc page-long-doc page-outline
# allpages tasks agenda sidebar; SIZES desktop (1280×800) phone (412×915 --touch); SCRIPTS (default
# per size: desktop "wheel touchpad", phone "drag flick"); PASSES (default 1); PLATFORM xvfb
# (default when xvfb-run exists) | offscreen (QT_QPA_PLATFORM=offscreen, software renderer);
# NOW (fixed clock, default 2026-10-14 10:00); DAYS (big vault, default 365); MAX_MS (time limit of
# one direction, default 60000); PROBE_XDG_DATA_DIRS (the app's XDG_DATA_DIRS — Qt searches icon
# themes there: a `nix develop` shell has ~70 entries, a NixOS session ~9, Android none); CACHE
# (--probe-cache: the list's cacheBuffer, 0 = every row created at the edge of the view; the runs
# are named …-cache<px>).
# Every run gets a fresh temporary XDG home: the user's own Brulion is never touched. One app
# instance at a time (lock), and run nothing else heavy meanwhile: the numbers are timings.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.."

if [[ -z "${BRULION_GUI_LOCKED:-}" ]] && command -v flock >/dev/null 2>&1; then
  export BRULION_GUI_LOCKED=1
  exec flock /tmp/brulion-gui.lock "$0" "$@"
fi

bin="${BRULION_BIN:-build/src/app/brulion}"
out="${OUT_DIR:-tests/screenshots/perf}"
vault_kind="${VAULT:-demo}"
now="${NOW:-2026-10-14 10:00}"
passes="${PASSES:-1}"
read -r -a sizes <<< "${SIZES:-desktop phone}"
if [[ $# -gt 0 ]]; then cases=("$@"); else read -r -a cases <<< "${CASES:-journals page-doc page-long-doc page-outline allpages tasks agenda sidebar}"; fi
platform="${PLATFORM:-}"
if [[ -z "$platform" ]]; then
  if command -v xvfb-run >/dev/null 2>&1; then platform=xvfb; else platform=offscreen; fi
fi
[[ -x "$bin" ]] || { echo "scroll-probe: $bin not found — build first" >&2; exit 1; }
mkdir -p "$out"

page_route() { printf '{"view":"page","path":"%s"}' "$1"; }
declare -A demo_pages=([page-doc]="Witaj w Brulionie.md" [page-long-doc]="Wyprawa w Bieszczady.md"
  [page-outline]="Projekty/Remont mieszkania.md")
declare -A big_pages=([page-doc]="Witaj w Brulionie.md" [page-long-doc]="Długi dokument.md"
  [page-outline]="Długi konspekt.md")

run_app() { # run_app <home> <args…>
  local home="$1"; shift
  local env=(XDG_CONFIG_HOME="$home/config" XDG_DATA_HOME="$home/data" XDG_CACHE_HOME="$home/cache" XDG_STATE_HOME="$home/state"
    XDG_DATA_DIRS="${PROBE_XDG_DATA_DIRS:-${XDG_DATA_DIRS:-}}"
    QT_FORCE_STDERR_LOGGING=1 QSG_INFO=1 QT_LOGGING_RULES="qt.scenegraph.time.renderloop=true")
  if [[ "$platform" == xvfb ]]; then
    env "${env[@]}" xvfb-run -a -s "-screen 0 1920x1200x24" "$bin" --no-single-instance "$@"
  else
    env "${env[@]}" QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software "$bin" --no-single-instance "$@"
  fi
}

prepare_vault() { # prepare_vault <home> → the notebook's path
  local home="$1" vault
  if [[ "$vault_kind" == big ]]; then
    vault="$home/Duży brulion"
    python3 tests/app/perf/make-vault.py "$vault" --days "${DAYS:-365}" --today "${now%% *}" >&2
  else
    vault="$home/Przykładowy brulion"
  fi
  # A first start seeds the sample notebook (and indexes the notebook), like a user's first run.
  run_app "$home" --demo --vault "$vault" --size 800x600 --now "$now" --delay 1500 \
    --screenshot "$home/prep.png" >/dev/null 2>&1 || true
  cp "tests/app/shell/notemode/Wyprawa w Bieszczady.md" "$vault/"
  mkdir -p "$vault/assets"
  cp tests/app/shell/notemode/bieszczady-trasa.svg "$vault/assets/"
  printf '%s' "$vault"
}

failed=0
for size in "${sizes[@]}"; do
  case "$size" in
    desktop) geometry=1280x800; extra=(); default_scripts="wheel touchpad" ;;
    phone) geometry=412x915; extra=(--touch); default_scripts="drag flick" ;;
    *) echo "scroll-probe: unknown size $size" >&2; exit 2 ;;
  esac
  read -r -a scripts <<< "${SCRIPTS:-$default_scripts}"
  for case_name in "${cases[@]}"; do
    target=auto; route=(); popup=()
    case "$case_name" in
      journals) route=(--route journals) ;;
      page-doc|page-long-doc|page-outline)
        if [[ "$vault_kind" == big ]]; then page="${big_pages[$case_name]}"; else page="${demo_pages[$case_name]}"; fi
        route=(--route "$(page_route "$page")") ;;
      allpages) route=(--route allPages) ;;
      tasks) route=(--route tasks) ;;
      agenda) route=(--route '{"view":"calendar","mode":"agenda"}') ;;
      sidebar) route=(--route journals); target=sidebar; [[ "$size" == phone ]] && popup=(--popup drawer) ;;
      *) echo "scroll-probe: unknown case $case_name" >&2; exit 2 ;;
    esac
    cache_args=()
    [[ -n "${CACHE:-}" ]] && cache_args=(--probe-cache "$CACHE")
    for script in "${scripts[@]}"; do
      name="$vault_kind-$case_name-$size-$script${CACHE:+-cache$CACHE}"
      home="$(mktemp -d)"
      vault="$(prepare_vault "$home")"
      echo "scroll-probe: $name ($platform)" >&2
      if run_app "$home" --vault "$vault" "${route[@]}" ${popup[@]+"${popup[@]}"} --size "$geometry" \
        --locale pl --now "$now" ${extra[@]+"${extra[@]}"} \
        --scroll-probe "$target" --scroll-script "$script" --probe-passes "$passes" --probe-max "${MAX_MS:-60000}" \
        ${cache_args[@]+"${cache_args[@]}"} \
        --probe-out "$out/$name.csv" > "$out/$name.log" 2>&1; then
        grep -E "^scroll-probe: (target|SUMMARY|FRAMES|JUMPS|SCROLL|VIEW|DELEGATES)" "$out/$name.log" >&2 || true
      else
        echo "scroll-probe: FAILED $name (see $out/$name.log)" >&2
        tail -5 "$out/$name.log" >&2 || true
        failed=1
      fi
      rm -rf "$home"
    done
  done
done
exit "$failed"
