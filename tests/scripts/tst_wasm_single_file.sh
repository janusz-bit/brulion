#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Tests scripts/wasm-single-file.py (the browser version in one HTML file, docs/WEB.md "Single HTML
# file") on stand-in builds: the page wasm/index.html as scripts/wasm-build.sh fills it in, its real
# icons, and small stand-ins for brulion.wasm, brulion.js and qtloader.js. The file must carry
# everything (brulion.wasm gzip + base64, byte for byte; the scripts inline before the page's own
# script; the pictures as data: URIs; no manifest, no other reference to a file of the site), be
# the same for the same input, and keep "</script" inside a script from ending it; a build it
# cannot pack safely is refused.
#
# Usage: tests/scripts/tst_wasm_single_file.sh [path/to/wasm-single-file.py [repository root]]
# Needs python3 (skipped with 77 without it).
set -euo pipefail

script=$(realpath -- "${1:-$(dirname -- "${BASH_SOURCE[0]}")/../../scripts/wasm-single-file.py}")
root=$(realpath -- "${2:-$(dirname -- "$script")/..}")
[[ -f $script && -f $root/wasm/index.html ]] || {
  echo "no $script or $root/wasm/index.html" >&2
  exit 2
}
command -v python3 >/dev/null || {
  echo "python3 not found: skipped"
  exit 77
}
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT

failed=0
pass() { printf 'PASS   : %s\n' "$1"; }
fail() {
  printf 'FAIL!  : %s\n' "$1"
  failed=$((failed + 1))
}
expect() { # <description> <command...>: the command succeeds
  local what=$1
  shift
  if "$@"; then pass "$what"; else fail "$what"; fi
}
# pack <dist> <out>: runs the script; sets $status and $out_text (stdout + stderr).
pack() {
  status=0
  out_text=$(python3 "$script" "$@" 2>&1) || status=$?
  printf '%s\n' "$out_text" | sed 's/^/        | /'
}
has() { grep -qF -- "$1" <<<"$out_text"; }

# A stand-in build as scripts/wasm-build.sh assembles it, with the real page and icons.
dist() { # <dir>
  mkdir -p "$1/icons"
  cp "$root"/wasm/icons/*.png "$1/icons/"
  cp "$root/assets/logo.svg" "$1/icons/logo.svg"
  cp "$root/wasm/manifest.webmanifest" "$1/"
  # Not a valid module: the script never looks inside. Every byte value, and more than one block.
  python3 -c 'import sys; sys.stdout.buffer.write(bytes(range(256)) * 300 + "zażółć gęślą jaźń".encode())' >"$1/brulion.wasm"
  printf '%s\n' "window.qtLoad = function (c) { return 'ładowanie'; };" >"$1/qtloader.js"
  printf '%s\n' "var brulion_entry = function () { return '</script><b>' + '</SCRIPT >'; };" >"$1/brulion.js"
  sed -e "s/@BRULION_WASM_SIZE@/$(wc -c <"$1/brulion.wasm")/" -e 's/@BRULION_ASSET_VERSION@/0123456789abcdef/' \
    "$root/wasm/index.html" >"$1/index.html"
}

# --- the real page ------------------------------------------------------------------------------
dist "$tmp/d"
pack "$tmp/d" "$tmp/out/brulion.html"
expect "packs the real page (exit 0)" test "$status" -eq 0
html=$tmp/out/brulion.html
expect "writes the file" test -s "$html"
expect "reports the size" has 'MiB'

# brulion.wasm comes back byte for byte (base64 → gunzip).
python3 - "$html" "$tmp/unpacked.wasm" <<'EOF'
import base64, gzip, re, sys
page = open(sys.argv[1], encoding="utf-8").read()
m = re.search(r'<script id="brulion-wasm" type="application/octet-stream">([A-Za-z0-9+/=]+)</script>', page)
open(sys.argv[2], "wb").write(gzip.decompress(base64.b64decode(m.group(1))) if m else b"")
EOF
expect "brulion.wasm unpacks byte for byte" cmp -s "$tmp/d/brulion.wasm" "$tmp/unpacked.wasm"

order=$(grep -noE 'id="brulion-wasm"|window\.qtLoad = |var brulion_entry = |'\''use strict'\''' "$html" | cut -d: -f2 | tr '\n' ' ')
expect "the data, qtloader.js and brulion.js come before the page's script ($order)" \
  test "$order" = "id=\"brulion-wasm\" window.qtLoad =  var brulion_entry =  'use strict' "
expect "the marker comment is gone" bash -c "! grep -q 'brulion-single-file' '$html'"
expect "no web app manifest" bash -c "! grep -q 'rel=\"manifest\"' '$html'"
expect "the logo is a data: URI" grep -q '<img src="data:image/svg+xml;base64,' "$html"
expect "the PNG icons are data: URIs" grep -q 'rel="apple-touch-icon" href="data:image/png;base64,' "$html"
refs=$(grep -oE '(src|href)="[^"]*"' "$html" | grep -vE '="(data:|https://|#)' || true)
expect "no reference to another file of the site (${refs:-none})" test -z "$refs"
# "</script" inside brulion.js is written "<\/script" (any case); the elements still end.
expect "\"</script\" in a script is escaped" grep -qF "'<\\/script><b>' + '<\\/SCRIPT >'" "$html"
expect "the page's text and Polish letters stay as they are" grep -qF "return 'ładowanie'" "$html"
closing=$(grep -oiE '</script' "$html" | wc -l)
opening=$(grep -oiE '<script[ >]' "$html" | wc -l)
expect "every <script> has its own </script> ($opening / $closing)" test "$opening" -eq "$closing"

pack "$tmp/d" "$tmp/again.html"
expect "the same input gives the same file" cmp -s "$html" "$tmp/again.html"

# --- refused builds -------------------------------------------------------------------------------
refused() { # <description> <text in the output> <setup command…>: a fresh build changed by the command fails
  local what=$1 text=$2
  shift 2
  rm -rf "$tmp/r" "$tmp/r.html"
  dist "$tmp/r"
  "$@"
  pack "$tmp/r" "$tmp/r.html"
  if [[ $status -eq 1 ]] && has "$text" && [[ ! -e $tmp/r.html ]]; then pass "$what"; else fail "$what"; fi
}
refused "the page from wasm/ with its placeholders" 'unreplaced placeholders' \
  cp "$root/wasm/index.html" "$tmp/r/index.html"
refused "no marker" 'must contain the marker' \
  sed -i 's/brulion-single-file/something-else/' "$tmp/r/index.html"
refused "\"<!--\" in brulion.js" "brulion.js contains '<!--'" \
  bash -c "printf 'var x = \"<!--\";\n' >>'$tmp/r/brulion.js'"
refused "another file of the site referenced" 'still references src="extra.png"' \
  sed -i 's|<div id="screen" hidden></div>|<img src="extra.png"><div id="screen" hidden></div>|' "$tmp/r/index.html"
refused "a missing icon" 'icon-192.png missing' rm "$tmp/r/icons/icon-192.png"
refused "a missing brulion.wasm" 'brulion.wasm missing' rm "$tmp/r/brulion.wasm"

echo
if ((failed)); then
  echo "Totals: $failed failed"
  exit 1
fi
echo "Totals: all passed"
