#!/usr/bin/env bash
# Screenshots of the sync center and import/export in the real app: SyncView (overview, Syncthing,
# Nextcloud with an unreachable server and the history of runs, a run's details, conflicts: a
# two-way merge, a binary file), ImportExportView (choosing the app, a Logseq graph previewed,
# export of a folder), the settings' sync and application sections and the event log page
# (`sync-stalled`, not in the default set: a server that never answers, the stall warning) —
# × {desktop 1280×800, phone 412×915 --touch} × {light, dark} into
# tests/screenshots/io-<shot>-<size>-<theme>.png.
#
# Every run gets a fresh, temporary XDG home (the sample notebook plus conflict copies and a
# Nextcloud account at chmura.invalid) and the fixed clock --now: reproducible images, the user's own
# Brulion untouched.
#
#   tests/app/io/screenshots.sh                       # everything
#   tests/app/io/screenshots.sh sync import-preview   # only these shots
#   SIZES=phone THEMES=dark tests/app/io/screenshots.sh sync-conflicts
#   On the laptop: scripts/remote-build.sh -n appio brulion \
#     --run 'BRULION_BIN=$BRULION_BUILD_DIR/src/app/brulion tests/app/io/screenshots.sh' \
#     --pull tests/screenshots
#
# Run inside `nix develop` (Qt plugin and QML paths). One app instance at a time (lock).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.."

if [[ -z "${BRULION_GUI_LOCKED:-}" ]] && command -v flock >/dev/null 2>&1; then
  export BRULION_GUI_LOCKED=1
  exec flock /tmp/brulion-gui.lock "$0" "$@"
fi

# shellcheck source=fixtures.sh
source tests/app/io/fixtures.sh

bin="${BRULION_BIN:-build/src/app/brulion}"
out="${OUT_DIR:-tests/screenshots}"
locale="${LOCALE:-pl}"
now="${NOW:-2026-10-14 10:00}"
delay="${DELAY:-1800}"
read -r -a sizes <<< "${SIZES:-desktop phone}"
read -r -a themes <<< "${THEMES:-light dark}"

graph="$(mktemp -d)/Graf Logseq"
io_logseq_graph "$graph"
template="$(mktemp -d)/template"

declare -A shot_routes=(
  [sync]='sync'
  [sync-syncthing]='{"view":"sync","tab":"syncthing"}'
  [sync-nextcloud]='{"view":"sync","tab":"nextcloud"}'
  [sync-conflicts]='{"view":"sync","copy":"Plan podróży.conflict-Telefon-Łucji-20261014-091500.md"}'
  [sync-conflict-merge]='{"view":"sync","copy":"Zakupy.sync-conflict-20261013-184500-LAPTOP7.md"}'
  [sync-conflict-binary]='{"view":"sync","copy":"assets/paragon.sync-conflict-20261013-184500-LAPTOP7.png"}'
  [sync-conflict-list]='{"view":"sync","tab":"conflicts"}'
  [import]='importExport'
  [import-notion]='{"view":"importExport","importer":"notion"}'
  [import-preview]="{\"view\":\"importExport\",\"files\":\"$graph\"}"
  [export]='{"view":"importExport","tab":"export","scope":"folder","folder":"Projekty"}'
  [settings-sync]='{"view":"settings","section":"sync"}'
  [sync-details]='{"view":"sync","run":40}'
  [sync-details-failed]='{"view":"sync","run":39}'
  [sync-stalled]='{"view":"sync","tab":"nextcloud"}'
  [logs]='logs'
  [logs-sync]='{"view":"logs","group":"sync"}'
  [settings-app]='{"view":"settings","section":"app"}'
)
all_shots=(sync sync-syncthing sync-nextcloud sync-conflicts sync-conflict-merge sync-conflict-binary
  sync-conflict-list import import-notion import-preview export settings-sync sync-details sync-details-failed
  logs logs-sync settings-app)
if [[ $# -gt 0 ]]; then shots=("$@"); else shots=("${all_shots[@]}"); fi

if [[ ! -x "$bin" ]]; then
  echo "screenshots: $bin not found — build first" >&2
  exit 1
fi
mkdir -p "$out"
io_seeded_vault "$bin" "$template" "$now"

suffix=""
[[ "$locale" != "pl" ]] && suffix+="-$locale"

failed=0
for shot in "${shots[@]}"; do
  [[ -n "${shot_routes[$shot]:-}" ]] || { echo "screenshots: unknown shot $shot" >&2; exit 2; }
  for size in "${sizes[@]}"; do
    case "$size" in
      desktop) geometry=1280x800; extra=() ;;
      phone) geometry=412x915; extra=(--touch) ;;
      desktop-tall) geometry=1280x2400; extra=() ;;
      phone-tall) geometry=412x3200; extra=(--touch) ;;
      *) echo "screenshots: unknown size $size" >&2; exit 2 ;;
    esac
    # The conflict list alone only exists on narrow windows.
    [[ "$shot" == sync-conflict-list && "$size" == desktop* ]] && continue
    for theme in "${themes[@]}"; do
      file="$out/io-${shot}-${size}-${theme}${suffix}.png"
      home="$(mktemp -d)"
      io_nextcloud_home "$home" "$home/Przykładowy brulion"
      cp -a "$template" "$home/Przykładowy brulion"
      io_history_base "$home" "$home/Przykładowy brulion"
      io_sync_history "$home" "$home/Przykładowy brulion"
      io_event_log "$home"
      shot_delay="$delay"
      stall_pid=""
      if [[ "$shot" == sync-stalled ]]; then
        # A local server that accepts connections and never answers: the live progress, then the
        # stall warning after 20 s (shown from ~22 s on). Not in the default set (slow).
        python3 -c '
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
s.listen(16)
print(s.getsockname()[1], flush=True)
held = []
while True:
    held.append(s.accept()[0])
' >"$home/stall-port" &
        stall_pid=$!
        for _ in $(seq 50); do [[ -s "$home/stall-port" ]] && break; sleep 0.1; done
        sed -i "s|^nextcloud\\\\url=.*|nextcloud\\\\url=http://127.0.0.1:$(cat "$home/stall-port")|" \
          "$home/config/Brulion/Brulion.ini"
        shot_delay=27000
      fi
      if XDG_CONFIG_HOME="$home/config" XDG_DATA_HOME="$home/data" XDG_CACHE_HOME="$home/cache" XDG_STATE_HOME="$home/state" \
        QML_DISABLE_DISK_CACHE=1 QT_FORCE_STDERR_LOGGING=1 QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
        "$bin" --demo --vault "$home/Przykładowy brulion" --route "${shot_routes[$shot]}" --no-single-instance \
        --size "$geometry" --theme "$theme" --locale "$locale" --now "$now" \
        --delay "$shot_delay" "${extra[@]}" --screenshot "$file" 2> >(grep -v "^brulion: screenshot" >&2); then
        echo "$file"
      else
        echo "screenshots: FAILED $file" >&2
        failed=1
      fi
      if [[ -n "$stall_pid" ]]; then
        kill "$stall_pid" 2>/dev/null || true
        wait "$stall_pid" 2>/dev/null || true
      fi
      rm -rf "$home"
    done
  done
done
exit "$failed"
