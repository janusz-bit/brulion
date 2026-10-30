#!/usr/bin/env bash
# QML smoke test of the sync and import/export views (ctest tst_appio_qml): SyncView on every tab
# (with conflict copies and an unreachable Nextcloud account), conflicts opened in the resolver (a
# note, a picture, a drawing, an Obsidian drawing whose notes differ),
# ImportExportView with a Logseq graph preset (reading, recognising, the preview), the Microsoft To
# Do card with its sign-in and the export
# tab, the settings' sync and application sections and the event log page — at desktop and phone
# sizes, headless. Fails when the app
# does not start or QML reports a warning or error from these files (binding loops, type errors,
# missing properties, layout loops).
#
#   tests/app/io/qml-smoke.sh <brulion binary> <scratch dir>
set -uo pipefail
bin="$1"
work="$2"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=fixtures.sh
source "$here/fixtures.sh"
rm -rf "$work"
mkdir -p "$work"
io_logseq_graph "$work/graf"
io_seeded_vault "$bin" "$work/template" "2026-10-14 10:00"

routes=(
  sync
  '{"view":"sync","tab":"syncthing"}'
  '{"view":"sync","tab":"nextcloud"}'
  '{"view":"sync","tab":"conflicts"}'
  '{"view":"sync","copy":"Plan podróży.conflict-Telefon-Łucji-20261014-091500.md"}'
  '{"view":"sync","copy":"assets/paragon.sync-conflict-20261013-184500-LAPTOP7.png"}'
  '{"view":"sync","copy":"Zakupy.sync-conflict-20261013-184500-LAPTOP7.md"}'
  '{"view":"sync","copy":"assets/Szkic.sync-conflict-20261013-184500-LAPTOP7.svg"}'
  '{"view":"sync","copy":"Excalidraw/Szkic ogrodu.excalidraw.sync-conflict-20261013-184500-LAPTOP7.md"}'
  importExport
  "{\"view\":\"importExport\",\"files\":\"$work/graf\"}"
  '{"view":"importExport","importer":"notion"}'
  '{"view":"importExport","importer":"mstodo"}'
  '{"view":"importExport","tab":"export","scope":"folder","folder":"Projekty"}'
  '{"view":"settings","section":"sync"}'
  '{"view":"settings","section":"app"}'
  '{"view":"sync","run":40}'
  logs
  '{"view":"logs","group":"sync","level":"warning"}'
)
# Also layout engine complaints (cells taken twice …): only these views are on screen.
pattern='qml/(io/|diag/|views/(SyncView|ImportExportView|SettingsView))|QGridLayoutEngine'

failed=0
for size in desktop phone; do
  if [[ "$size" == desktop ]]; then geometry=(--size 1280x800); else geometry=(--size 412x915 --touch); fi
  for route in "${routes[@]}"; do
    home="$work/home"
    rm -rf "$home"
    vault="$home/Przykładowy brulion"
    io_nextcloud_home "$home" "$vault"
    cp -a "$work/template" "$vault"
    io_history_base "$home" "$vault"
    io_sync_history "$home" "$vault"
    io_event_log "$home"
    log="$work/log.txt"
    if ! XDG_CONFIG_HOME="$home/config" XDG_DATA_HOME="$home/data" XDG_CACHE_HOME="$home/cache" XDG_STATE_HOME="$home/state" \
      "$bin" --demo --vault "$vault" --route "$route" --no-single-instance "${geometry[@]}" \
      --now "2026-10-14 10:00" --locale pl --delay 1800 --screenshot "$work/shot.png" 2> "$log"; then
      echo "FAIL: the app did not run for $route ($size)"
      cat "$log"
      failed=$((failed + 1))
      continue
    fi
    if grep -E "$pattern" "$log"; then
      echo "FAIL: QML warnings for $route ($size)"
      failed=$((failed + 1))
    else
      echo "ok: $route ($size)"
    fi
  done
done
exit "$failed"
