#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Vendors a release of HarfBuzz for Atrament's text (3rdparty/harfbuzz, docs/ATRAMENT.md D7):
# downloads the tag's source archive, writes its SHA-256 into 3rdparty/harfbuzz/pin.json, and
# copies into 3rdparty/harfbuzz/src only the files the amalgamation 3rdparty/harfbuzz/harfbuzz.cc
# needs — upstream's src/harfbuzz.cc without the integrations Brulion never uses (FreeType, glib,
# ICU, Graphite, CoreText, DirectWrite, GDI, Uniscribe, WebAssembly shapers; subsetting, the
# raster/vector/GPU libraries and the tools are not in it anyway). The file list is the
# preprocessor's: what `c++ -MM` reads for the amalgamation, for the desktop and as Windows,
# Android, the browser and Apple would see it. Then build and run Atrament's tests
# (`scripts/dev-build.sh -t tst_atrament`, goldens: BRULION_ATRAMENT_UPDATE_GOLDENS=1 when a new
# release shapes or draws differently on purpose).
#
# Usage: scripts/harfbuzz-vendor.sh <tag>          e.g. scripts/harfbuzz-vendor.sh 14.5.1
# Needs curl, sha256sum, tar, python3 and a C++ compiler ($CXX, else c++).
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
DEST=$ROOT/3rdparty/harfbuzz

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

case ${1:-} in
  -h | --help | "")
    awk 'NR > 2 && /^#/ { sub(/^# ?/, ""); print; next } NR > 2 { exit }' "${BASH_SOURCE[0]}"
    [[ -n ${1:-} ]] && exit 0
    exit 2
    ;;
esac
tag=$1
[[ $tag =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "not a HarfBuzz release tag: $tag"
url=https://github.com/harfbuzz/harfbuzz/archive/refs/tags/$tag.tar.gz
cxx=${CXX:-c++}
command -v "$cxx" >/dev/null || die "no C++ compiler ($cxx); run in the dev shell (nix develop)"

tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT
echo "downloading $url" >&2
curl --fail --location --silent --show-error --retry 3 -o "$tmp/harfbuzz.tar.gz" "$url" || die "cannot download $url"
sha256=$(sha256sum "$tmp/harfbuzz.tar.gz" | cut -d' ' -f1)
mkdir "$tmp/src"
tar -xzf "$tmp/harfbuzz.tar.gz" -C "$tmp/src" --strip-components=1
up=$tmp/src/src
[[ -f $up/harfbuzz.cc && -f $up/hb.h ]] || die "$url: no src/harfbuzz.cc inside"
grep -q "#define HB_VERSION_STRING \"$tag\"" "$up/hb-version.h" || die "$url: src/hb-version.h is not $tag's"

# The amalgamation: upstream's, without the integrations (each also compiles to nothing without
# its HAVE_… macro, but then its sources need not be here).
amalgamation=$tmp/harfbuzz.cc
{
  echo "// HarfBuzz $tag in one translation unit (3rdparty/harfbuzz/CMakeLists.txt): upstream's"
  echo "// src/harfbuzz.cc without the integrations Brulion does not use. Written by"
  echo "// scripts/harfbuzz-vendor.sh — do not edit."
  grep -E '^#include "' "$up/harfbuzz.cc" |
    grep -v -E '"hb-(coretext|directwrite|ft|gdi|glib|graphite2|uniscribe|wasm)[-.a-z]*\.cc"'
} >"$amalgamation"
[[ $(grep -c '^#include' "$amalgamation") -gt 40 ]] || die "unexpected src/harfbuzz.cc in $tag"

# Every file of src/ the amalgamation reads, whatever the platform. The desktop's list must come
# out whole; the others are this compiler pretending (-MG: <windows.h> and friends are missing
# here, and the expressions over their macros fail — they are the system's, not HarfBuzz's).
files=$tmp/files
(cd "$up" && "$cxx" -std=c++17 -MM -MG -I. "$amalgamation") >"$tmp/deps" || die "$cxx -MM failed on the amalgamation"
for defines in "-D_WIN32 -D_MSC_VER=1940" "-D__ANDROID__" "-D__EMSCRIPTEN__" "-D__APPLE__"; do
  # shellcheck disable=SC2086 # the defines are words
  (cd "$up" && "$cxx" -std=c++17 -MM -MG $defines -I. "$amalgamation" 2>/dev/null) >>"$tmp/deps" || true
done
tr ' \\' '\n\n' <"$tmp/deps" | grep -v -E '^$|:$' | sort -u |
  python3 -I -c '
import os, sys
up = sys.argv[1]
for line in sys.stdin:
    path = os.path.normpath(os.path.join(up, line.strip()))
    if os.path.isfile(path) and os.path.commonpath([up, path]) == up:
        print(os.path.relpath(path, up))
' "$up" | sort -u >"$files"
grep -qx 'hb.h' "$files" && grep -qx 'hb-ot-shape.cc' "$files" || die "the file list misses hb.h or hb-ot-shape.cc"

rm -rf -- "$DEST/src"
mkdir -p "$DEST/src"
(cd "$up" && tar -cf - -T "$files") | (cd "$DEST/src" && tar -xf -)
cp -- "$amalgamation" "$DEST/harfbuzz.cc"
cp -- "$tmp/src/COPYING" "$DEST/COPYING"

python3 -I - "$DEST/pin.json" "$tag" "$url" "$sha256" <<'PY'
import json, sys
path, tag, url, sha256 = sys.argv[1:]
pin = {
    "!comment": "HarfBuzz, which shapes Atrament's text and gives its glyphs' outlines (3rdparty/harfbuzz/CMakeLists.txt): one release, vendored — src/ holds the files of the release archive below that harfbuzz.cc needs, unchanged. `sha256` is the archive's. Update: scripts/harfbuzz-vendor.sh <tag>.",
    "owner": "harfbuzz",
    "repo": "harfbuzz",
    "tag": tag,
    "url": url,
    "sha256": sha256,
}
with open(path, "w", encoding="utf-8") as f:
    json.dump(pin, f, indent=2, ensure_ascii=False)
    f.write("\n")
PY
count=$(wc -l <"$files")
size=$(du -sh "$DEST/src" | cut -f1)
echo "3rdparty/harfbuzz: HarfBuzz $tag, $count files ($size), sha256 $sha256"
