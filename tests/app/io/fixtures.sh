#!/usr/bin/env bash
# Fixtures shared by qml-smoke.sh and screenshots.sh of the sync and import/export views.
#
#   source tests/app/io/fixtures.sh
#   io_conflict_vault <dir>     conflict copies: Syncthing ones of "Zakupy.md" and of a picture, a
#                               Brulion (Nextcloud) one of "Plan podróży.md"
#   io_seeded_vault <bin> <dir> <now>   once: the sample notebook seeded by the app itself (--demo
#                               only seeds an empty folder) plus io_conflict_vault — copy it per run
#   io_history_base <home> <vault>   this device's version history of "Zakupy.md" holds the common
#                               ancestor of both versions (written in History's on-disk format)
#   io_logseq_graph <dir>       a small Logseq graph to import (journal, pages, a picture)
#   io_nextcloud_home <home>    settings of an XDG home with a Nextcloud account whose server
#                               does not exist (chmura.invalid: fails at once, offline-safe)

io_conflict_vault() {
  local dir="$1"
  mkdir -p "$dir/assets"
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
  local home="$1"
  mkdir -p "$home/config/Brulion" "$home/data/Brulion/Brulion/secrets"
  cat > "$home/config/Brulion/Brulion.ini" <<'EOF'
[sync]
nextcloud\enabled=true
nextcloud\url=https://chmura.invalid
nextcloud\user=jan
nextcloud\folder=Notatki
intervalMinutes=15
EOF
  printf 'abcde-fghij-klmno-pqrst-uvwxy' > "$home/data/Brulion/Brulion/secrets/nextcloud-password"
  chmod 600 "$home/data/Brulion/Brulion/secrets/nextcloud-password"
}

io_seeded_vault() {
  local bin="$1" dir="$2" now="$3"
  [[ -d "$dir" ]] && return 0
  local home
  home="$(mktemp -d)"
  XDG_CONFIG_HOME="$home/config" XDG_DATA_HOME="$home/data" XDG_CACHE_HOME="$home/cache" \
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
