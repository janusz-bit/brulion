#!/usr/bin/env bash
# (The checks run through wait_for, which shellcheck cannot follow.)
# shellcheck disable=SC2329
#
# A real Syncthing conflict of drawings, merged by the sync center ("Scal rysunki", docs/UI.md
# "Conflict copies of drawings"). Two Syncthing instances on this machine share one folder over
# 127.0.0.1 (no discovery, no relays): a Brulion drawing (tests/data/drawing/conflict_*.svg) and an
# Obsidian Excalidraw plugin file (obsidian_json.excalidraw.md with a line added on each side)
# change on both while the devices are paused; once resumed, Syncthing leaves its own conflict
# copies. tst_appio_conflicts (realSyncthingConflicts) then merges them in a copy of each device's
# folder. Not part of ctest: it needs `syncthing` (2.x) and `curl` in PATH and takes ~1 minute.
#
#   tests/app/io/syncthing-conflict.sh <tst_appio_conflicts binary>
#   On the laptop: scripts/remote-build.sh -n <name> tst_appio_conflicts \
#     --run 'tests/app/io/syncthing-conflict.sh $BRULION_BUILD_DIR/tests/app/io/tst_appio_conflicts'
set -euo pipefail
test_bin="$1"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
drawings="$here/../../data/drawing"
work="$(mktemp -d)"
pids=()
cleanup() {
  for pid in "${pids[@]}"; do kill "$pid" 2>/dev/null || true; done
  wait 2>/dev/null || true
  rm -rf "$work"
}
trap cleanup EXIT

# Free ports from a random base (two GUIs, two listeners).
base=$((20000 + RANDOM % 20000))
gui_a=$base; gui_b=$((base + 1)); listen_a=$((base + 2)); listen_b=$((base + 3))
key_a="brulion-a-$RANDOM$RANDOM"; key_b="brulion-b-$RANDOM$RANDOM"

for d in a b; do
  syncthing generate --home="$work/$d" --no-port-probing >/dev/null
  mkdir -p "$work/vault-$d/assets" "$work/vault-$d/Excalidraw"
done
id_a="$(syncthing device-id --home="$work/a")"
id_b="$(syncthing device-id --home="$work/b")"

# Each config: its own listener, the other device at a fixed address, the shared folder, nothing
# else (no default folder, discovery, relays, NAT or usage reports).
configure() {
  python3 -I - "$@" <<'PY'
import sys, xml.etree.ElementTree as ET
config, listen, folder, me, other, other_addr = sys.argv[1:7]
tree = ET.parse(config)
root = tree.getroot()
for f in root.findall("folder"):
    root.remove(f)
opts = root.find("options")
for tag in ("listenAddress",):
    for e in opts.findall(tag):
        opts.remove(e)
ET.SubElement(opts, "listenAddress").text = listen
for tag, value in (("globalAnnounceEnabled", "false"), ("localAnnounceEnabled", "false"),
                   ("relaysEnabled", "false"), ("natEnabled", "false"), ("urAccepted", "-1"),
                   ("crashReportingEnabled", "false"), ("autoUpgradeIntervalH", "0"),
                   ("startBrowser", "false")):
    e = opts.find(tag)
    if e is None:
        e = ET.SubElement(opts, tag)
    e.text = value
folder_el = ET.Element("folder", {"id": "brulion", "label": "brulion", "path": folder, "type": "sendreceive",
                                   "rescanIntervalS": "3600", "fsWatcherEnabled": "false"})
for dev in (me, other):
    ET.SubElement(folder_el, "device", {"id": dev})
root.insert(0, folder_el)
dev_el = ET.Element("device", {"id": other, "name": "other", "compression": "metadata", "introducer": "false"})
ET.SubElement(dev_el, "address").text = other_addr
root.insert(1, dev_el)
tree.write(config)
PY
}
configure "$work/a/config.xml" "tcp://127.0.0.1:$listen_a" "$work/vault-a" "$id_a" "$id_b" "tcp://127.0.0.1:$listen_b"
configure "$work/b/config.xml" "tcp://127.0.0.1:$listen_b" "$work/vault-b" "$id_b" "$id_a" "tcp://127.0.0.1:$listen_a"

syncthing serve --home="$work/a" --no-browser --no-restart --no-upgrade --gui-address="http://127.0.0.1:$gui_a" \
  --gui-apikey="$key_a" --log-file="$work/a.log" >/dev/null 2>&1 &
pids+=($!)
syncthing serve --home="$work/b" --no-browser --no-restart --no-upgrade --gui-address="http://127.0.0.1:$gui_b" \
  --gui-apikey="$key_b" --log-file="$work/b.log" >/dev/null 2>&1 &
pids+=($!)

api() { # api a|b METHOD path
  local port key
  if [[ "$1" == a ]]; then port=$gui_a; key=$key_a; else port=$gui_b; key=$key_b; fi
  curl -sf -X "$2" -H "X-API-Key: $key" "http://127.0.0.1:$port$3"
}
wait_for() { # wait_for <what> <command…>: up to 120 s
  local what="$1"; shift
  for _ in $(seq 240); do
    if "$@" >/dev/null 2>&1; then return 0; fi
    sleep 0.5
  done
  echo "syncthing-conflict: timed out waiting for $what" >&2
  tail -n 30 "$work/a.log" "$work/b.log" >&2 || true
  exit 1
}
connected() { api "$1" GET /rest/system/connections | grep -q '"connected": *true'; }
idle() { api "$1" GET "/rest/db/status?folder=brulion" | grep -q '"state": *"idle"'; }
in_sync() { # both idle, nothing needed, and B has what A has
  idle a && idle b && api a GET "/rest/db/status?folder=brulion" | grep -q '"needTotalItems": *0' \
    && api b GET "/rest/db/status?folder=brulion" | grep -q '"needTotalItems": *0'
}
scan() { api "$1" POST "/rest/db/scan?folder=brulion" >/dev/null; }

wait_for "syncthing A" api a GET /rest/system/ping
wait_for "syncthing B" api b GET /rest/system/ping
wait_for "A and B to connect" connected a

# The drawings both devices start from.
cp "$drawings/excalidraw_freedraw.svg" "$work/vault-a/assets/Szkic.svg"
cp "$drawings/obsidian_json.excalidraw.md" "$work/vault-a/Excalidraw/Szkic ogrodu.excalidraw.md"
scan a
wait_for "the drawings to reach B" test -f "$work/vault-b/Excalidraw/Szkic ogrodu.excalidraw.md"
wait_for "A and B in sync" in_sync

# Offline from each other, each device changes both drawings.
api a POST "/rest/system/pause?device=$id_b" >/dev/null
api b POST "/rest/system/pause?device=$id_a" >/dev/null
add_line() { # add_line <plugin file> <id>: a freehand line drawn on this device
  python3 -I - "$1" "$2" <<'PY'
import json, re, sys
path, new_id = sys.argv[1:3]
text = open(path, encoding="utf-8").read()
m = re.search(r"```json\n(.*?)\n```", text, re.S)
scene = json.loads(m.group(1))
line = dict(scene["elements"][0])
line.update(id=new_id, y=line["y"] + 140 + 60 * len(scene["elements"]), version=1, versionNonce=7, seed=11)
scene["elements"].append(line)
text = text[:m.start(1)] + json.dumps(scene, indent="\t", ensure_ascii=False) + text[m.end(1):]
open(path, "w", encoding="utf-8").write(text)
PY
}
cp "$drawings/conflict_ours.svg" "$work/vault-a/assets/Szkic.svg"
add_line "$work/vault-a/Excalidraw/Szkic ogrodu.excalidraw.md" "kreska-z-laptopa"
sleep 1.2
cp "$drawings/conflict_theirs.svg" "$work/vault-b/assets/Szkic.svg"
add_line "$work/vault-b/Excalidraw/Szkic ogrodu.excalidraw.md" "kreska-z-telefonu-ż"
scan a
scan b
wait_for "A to scan" idle a
wait_for "B to scan" idle b
api a POST "/rest/system/resume?device=$id_b" >/dev/null
api b POST "/rest/system/resume?device=$id_a" >/dev/null
wait_for "A and B to connect again" connected a
copies() { [[ $(find "$work/vault-$1" -name '*.sync-conflict-*' | wc -l) -eq 2 ]]; }
wait_for "conflict copies on A" copies a
wait_for "conflict copies on B" copies b
wait_for "A and B in sync" in_sync

status=0
for d in a b; do
  echo "== device $(tr a-b A-B <<< "$d"): $(cd "$work/vault-$d" && find . -type f -not -path './.st*' | sort | tr '\n' ' ')"
  BRULION_SYNCTHING_CONFLICT_DIR="$work/vault-$d" QT_QPA_PLATFORM=offscreen \
    "$test_bin" realSyncthingConflicts || status=1
done
exit $status
