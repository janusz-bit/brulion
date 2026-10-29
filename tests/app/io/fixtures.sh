#!/usr/bin/env bash
# Fixtures shared by qml-smoke.sh and screenshots.sh of the sync and import/export views.
#
#   source tests/app/io/fixtures.sh
#   io_conflict_vault <dir>     conflict copies: Syncthing ones of "Zakupy.md", of a picture and of
#                               a drawing ("assets/Szkic.svg": tests/data/drawing/conflict_*.svg),
#                               a Brulion (Nextcloud) one of "Plan podróży.md"
#   io_seeded_vault <bin> <dir> <now>   once: the sample notebook seeded by the app itself (--demo
#                               only seeds an empty folder) plus io_conflict_vault — copy it per run
#   io_history_base <home> <vault>   this device's version history of "Zakupy.md" holds the common
#                               ancestor of both versions (written in History's on-disk format)
#   io_logseq_graph <dir>       a small Logseq graph to import (journal, pages, a picture)
#   io_nextcloud_home <home> <vault>  settings of an XDG home with a Nextcloud account whose
#                               server does not exist (chmura.invalid: fails at once, offline-safe),
#                               written as 0.3.0 did with <vault> open: it takes them over
#   io_sync_history <home> <vault>  the sync history of this device (sync-history.json in the
#                               vault's device state): runs #37–#43 of 2026-10-14 — ok, quiet ones
#                               folded, per-file errors, a timeout, a stalled run
#   io_event_log <home>         an earlier session in the event log (XDG_STATE_HOME/…/logs/brulion.log):
#                               a slow sync that timed out, a retry, a stall

io_conflict_vault() {
  local dir="$1" drawings
  drawings="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../data/drawing" && pwd)"
  mkdir -p "$dir/assets"
  # A drawing changed on two devices: a line and a box in both, a blue line here, a red ellipse in
  # the copy ("Scal rysunki").
  cp "$drawings/conflict_ours.svg" "$dir/assets/Szkic.svg"
  cp "$drawings/conflict_theirs.svg" "$dir/assets/Szkic.sync-conflict-20261013-184500-LAPTOP7.svg"
  # Zakupy: with io_history_base an automatic three-way merge (both appended after "masło", only
  # this device changed the bread); without it a two-way merge.
  printf -- '- mleko\n- chleb żytni\n- masło\n- jajka\n- kawa ziarnista\n' > "$dir/Zakupy.md"
  printf -- '- mleko\n- chleb\n- masło\n- ser żółty\n- kawa ziarnista\n- herbata\n' \
    > "$dir/Zakupy.sync-conflict-20261013-184500-LAPTOP7.md"
  printf -- '# Plan podróży do Gdańska\n\n- Pociąg w piątek o 7:15\n- Nocleg na dwie noce\n' \
    > "$dir/Plan podróży.md"
  printf -- '# Plan podróży do Gdańska\n\n- Pociąg w czwartek o 7:15\n- Nocleg na trzy noce\n- Europejskie Centrum Solidarności\n' \
    > "$dir/Plan podróży.conflict-Telefon-Łucji-20261014-091500.md"
  # Two real pictures (a receipt in two shades) for the side-by-side preview.
  python3 - "$dir/assets/paragon.png" "$dir/assets/paragon.sync-conflict-20261013-184500-LAPTOP7.png" <<'PY'
import struct, sys, zlib
def png(path, rgb, w=120, h=80):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)
    rows = b"".join(b"\x00" + bytes(rgb) * w for _ in range(h))
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
    open(path, "wb").write(data)
png(sys.argv[1], (226, 87, 76))
png(sys.argv[2], (159, 195, 230), 90, 120)
PY
}

io_logseq_graph() {
  local dir="$1"
  mkdir -p "$dir/logseq" "$dir/journals" "$dir/pages" "$dir/assets"
  printf '{:journal/file-name-format "yyyy_MM_dd"}\n' > "$dir/logseq/config.edn"
  printf -- '- Spotkanie z Łucją o źródłach finansowania\n- TODO kupić żółty ser\n' > "$dir/journals/2026_10_14.md"
  printf -- '- Rano: przegląd zadań\n' > "$dir/journals/2026_10_12.md"
  printf -- '- mleko\n- chleb ![paragon](../assets/paragon.png)\n' > "$dir/pages/Zakupy.md"
  printf -- '- [[Zakupy]] na święta\n- [[Książki 2026]]\n' > "$dir/pages/Pomysły.md"
  printf -- '- „Lalka” — Bolesław Prus\n' > "$dir/pages/Książki 2026.md"
  printf '\x89PNG' > "$dir/assets/paragon.png"
}

io_nextcloud_home() {
  local home="$1" vault="$2"
  mkdir -p "$home/config/Brulion" "$home/data/Brulion/Brulion/secrets"
  cat > "$home/config/Brulion/Brulion.ini" <<'EOF'
[sync]
nextcloud\enabled=true
nextcloud\url=https://chmura.invalid
nextcloud\user=jan
nextcloud\folder=Notatki
intervalMinutes=15
EOF
  printf '\n[vaults]\ncurrent="%s"\n' "$vault" >> "$home/config/Brulion/Brulion.ini"
  printf 'abcde-fghij-klmno-pqrst-uvwxy' > "$home/data/Brulion/Brulion/secrets/nextcloud-password"
  chmod 600 "$home/data/Brulion/Brulion/secrets/nextcloud-password"
}

io_seeded_vault() {
  local bin="$1" dir="$2" now="$3"
  [[ -d "$dir" ]] && return 0
  local home
  home="$(mktemp -d)"
  XDG_CONFIG_HOME="$home/config" XDG_DATA_HOME="$home/data" XDG_CACHE_HOME="$home/cache" XDG_STATE_HOME="$home/state" \
    QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
    "$bin" --demo --vault "$dir" --no-single-instance --size 400x400 --now "$now" --delay 200 \
    --screenshot "$home/seed.png" >/dev/null 2>&1 || true
  rm -rf "$home"
  io_conflict_vault "$dir"
}

io_history_base() {
  local home="$1" vault="$2"
  python3 - "$home/data/Brulion/Brulion" "$vault" <<'PY'
import hashlib, json, os, sys
data, vault = sys.argv[1], sys.argv[2]
def fnv(b):
    h = 0xcbf29ce484222325
    for x in b:
        h = ((h ^ x) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % h
state = os.path.join(data, "vaults", fnv(os.path.realpath(vault).encode()))
hist = os.path.join(state, "history")
os.makedirs(os.path.join(hist, "index"), exist_ok=True)
os.makedirs(os.path.join(hist, "objects"), exist_ok=True)
text = "- mleko\n- chleb\n- masło\n- kawa ziarnista\n".encode()
time = 1791880200000  # 2026-10-13 08:30 UTC (10:30 in Warsaw)
def b36(n):
    d = "0123456789abcdefghijklmnopqrstuvwxyz"
    s = ""
    while n:
        n, r = divmod(n, 36)
        s = d[r] + s
    return s or "0"
eid = b36(time) + "-0001abcd"
open(os.path.join(hist, "objects", eid + ".txt"), "wb").write(text)
index = {"path": "Zakupy.md", "entries": [{"id": eid, "time": time, "size": len(text), "reason": "external", "hash": fnv(text)}]}
name = hashlib.sha1("Zakupy.md".encode()).hexdigest() + ".json"
open(os.path.join(hist, "index", name), "w").write(json.dumps(index))
PY
}

io_state_dir() {
  local home="$1" vault="$2"
  python3 - "$home/data/Brulion/Brulion" "$vault" <<'PY'
import os, sys
data, vault = sys.argv[1], sys.argv[2]
def fnv(b):
    h = 0xcbf29ce484222325
    for x in b:
        h = ((h ^ x) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % h
print(os.path.join(data, "vaults", fnv(os.path.realpath(vault).encode())))
PY
}

io_sync_history() {
  local home="$1" vault="$2" state
  state="$(io_state_dir "$home" "$vault")"
  mkdir -p "$state"
  python3 - "$state/sync-history.json" <<'PY'
import json, sys
def phases(*items):
    names = ["connect", "list-remote", "list-local", "compare", "transfer", "save-state"]
    out, start = [], 0
    for name, (d, i, b) in zip(names, items):
        out.append({"p": name, "s": start, "d": d, "i": i, "b": b})
        start += d
    return out
def stats(requests, methods, ms, sent, received, listing, slowest, timeouts=0, failed=0, stall=0):
    return {"requests": requests, "failed": failed, "timeouts": timeouts, "retries": 0, "methods": methods,
            "requestMs": ms, "sent": sent, "received": received, "listing": listing, "folders": 41,
            "remoteFiles": 3120, "localFiles": 3098, "tracked": 3110, "paths": 3125, "stall": stall,
            "protocol": "h2", "slowest": slowest}
runs = [
    {"n": 43, "trigger": "timer", "started": "2026-10-14T09:55:02.000", "finished": "2026-10-14T09:55:11.400",
     "ms": 9400, "outcome": "ok", "up": 0, "down": 0, "merged": 0, "delLocal": 0, "delRemote": 0, "conflicts": 0,
     "fileErrors": 0, "skipped": 0, "pending": 0, "repeats": 3, "firstStarted": "2026-10-14T09:25:01.000",
     "phases": phases((180, 0, 0), (8400, 41, 1468000), (420, 3098, 0), (90, 3110, 0), (250, 3125, 0), (60, 0, 0)),
     "stats": stats(42, {"PROPFIND": 42}, 11800, 8100, 1468000, 1468000,
                    [{"m": "PROPFIND", "p": "journals/", "s": 207, "ms": 1900, "b": 412000}])},
    {"n": 40, "trigger": "retry", "started": "2026-10-14T09:12:30.000", "finished": "2026-10-14T09:13:44.100",
     "ms": 74100, "outcome": "ok", "up": 3, "down": 1, "merged": 1, "delLocal": 0, "delRemote": 0, "conflicts": 0,
     "fileErrors": 0, "skipped": 0, "pending": 0,
     "phases": phases((210, 0, 0), (61200, 41, 1470000), (510, 3098, 0), (95, 3110, 0), (11800, 3125, 24600), (290, 0, 0)),
     "stats": stats(51, {"GET": 2, "MKCOL": 1, "PROPFIND": 44, "PUT": 4}, 98000, 30100, 1490000, 1470000,
                    [{"m": "PROPFIND", "p": "journals/", "s": 207, "ms": 48200, "b": 412000},
                     {"m": "PUT", "p": "journals/2026_10_14.md", "s": 201, "ms": 6100, "b": 2100}])},
    {"n": 39, "trigger": "network", "started": "2026-10-14T09:10:58.000", "finished": "2026-10-14T09:12:00.300",
     "ms": 62300, "outcome": "failed", "code": "timeout", "status": 0,
     "message": "PROPFIND https://chmura.invalid/remote.php/dav/files/jan/Notatki/journals/ timed out after 60 s",
     "up": 0, "down": 0, "merged": 0, "delLocal": 0, "delRemote": 0, "conflicts": 0, "fileErrors": 0, "skipped": 0,
     "pending": 0, "phases": phases((240, 0, 0), (62000, 17, 610000)),
     "stats": stats(18, {"PROPFIND": 18}, 71000, 3500, 610000, 610000,
                    [{"m": "PROPFIND", "p": "journals/", "s": 0, "ms": 60000, "b": 0}], timeouts=1, failed=1, stall=60000)},
    {"n": 38, "trigger": "edit", "started": "2026-10-14T08:41:20.000", "finished": "2026-10-14T08:41:34.700",
     "ms": 14700, "outcome": "files", "code": "files", "up": 2, "down": 0, "merged": 0, "delLocal": 0, "delRemote": 0,
     "conflicts": 0, "fileErrors": 1, "skipped": 0, "pending": 0,
     "errors": [{"path": "Projekty/Remont mieszkania.md", "message": "PUT Projekty/Remont mieszkania.md failed: HTTP 423",
                 "code": "locked", "status": 423}],
     "phases": phases((150, 0, 0), (9100, 41, 1468000), (430, 3098, 0), (90, 3110, 0), (4800, 3125, 6200), (130, 0, 0)),
     "stats": stats(47, {"PROPFIND": 42, "PUT": 3, "MKCOL": 2}, 15800, 14300, 1471000, 1468000,
                    [{"m": "PROPFIND", "p": "journals/", "s": 207, "ms": 2100, "b": 412000}], failed=1)},
    {"n": 37, "trigger": "start", "started": "2026-10-14T08:30:05.000", "finished": "2026-10-14T08:33:07.000",
     "ms": 182000, "outcome": "failed", "code": "stalled",
     "message": "No answer or progress for 180 s; the run was stopped",
     "up": 0, "down": 0, "merged": 0, "delLocal": 0, "delRemote": 0, "conflicts": 0, "fileErrors": 0, "skipped": 0,
     "pending": 0, "phases": phases((300, 0, 0), (181700, 9, 220000)),
     "stats": stats(10, {"PROPFIND": 10}, 190000, 1900, 220000, 220000,
                    [{"m": "PROPFIND", "p": "journals/", "s": 0, "ms": 180000, "b": 0}], failed=1, stall=180000)},
]
json.dump({"runs": runs}, open(sys.argv[1], "w"), ensure_ascii=False)
PY
}

io_event_log() {
  local home="$1" dir="$1/state/Brulion/Brulion/logs"
  mkdir -p "$dir"
  cat > "$dir/brulion.log" <<'EOF'
2026-10-14 09:10:41.512	I	brulion.app	=== session start: Brulion 0.2.1 (Android 15, arm64, Qt 6.11.2), pid 8121, UTC+02:00 ===
2026-10-14 09:10:42.020	I	brulion.vault	Notebook opened: 3098 files, 41 folders in 1.2 s
2026-10-14 09:10:42.101	I	brulion.sync	Nextcloud sync: user jan at https://chmura.invalid, folder Notatki, every 15 min
2026-10-14 09:10:58.004	I	brulion.sync	#39 started (network)
2026-10-14 09:11:20.010	W	brulion.sync	#39 no answer from the server for 20 s (waiting for: PROPFIND journals/)
2026-10-14 09:11:58.300	W	brulion.net	#39 PROPFIND journals/ timed out after 60 s
2026-10-14 09:12:00.300	W	brulion.sync	#39 failed after 62.3 s (network): timeout — PROPFIND https://chmura.invalid/remote.php/dav/files/jan/Notatki/journals/ timed out after 60 s
2026-10-14 09:12:00.302	I	brulion.sync	retry in 30 s after 1 failed run (timeout)
2026-10-14 09:12:30.001	I	brulion.sync	#40 started (retry)
2026-10-14 09:13:00.020	I	brulion.sync	#40 still running after 30.0 s: list-remote, folder 31 of 41, 1 in flight (PROPFIND journals/)
2026-10-14 09:13:18.400	I	brulion.net	#40 slow answer: PROPFIND journals/ -> 207 in 48.2 s (402.3 KB)
2026-10-14 09:13:44.100	I	brulion.sync	#40 ok in 74.1 s (retry): up 3, down 1, merged 1, deleted 0/0, conflicts 0, errors 0 | remote 61.2 s (41 folders, 3120 files, 1.4 MB) · local 510 ms (3098 files) · compare 95 ms · transfer 11.8 s (3125 paths, 24.0 KB) · save 290 ms | 51 requests (GET 2, MKCOL 1, PROPFIND 44, PUT 4), avg 1.9 s, slowest PROPFIND journals/ 48.2 s, h2
2026-10-14 09:14:02.000	I	brulion.app	App in the background
EOF
}
