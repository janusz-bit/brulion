#!/usr/bin/env bash
# The round trip of a Brulion drawing through the real excalidraw.com (excalidraw-roundtrip.mjs):
#  1. tst_drawingcanvas shapesSample draws a diagram with the editor's tools (shapes, a line,
#     arrows, free text, labels, a pen stroke, a copy, a recoloured and a moved shape) → brulion.svg;
#  2. excalidraw.com opens it (dropped on its canvas) and keeps every element as written →
#     excalidraw.excalidraw (its scene as it stores it);
#  3. Brulion opens excalidraw.com's scene editable, edits it and writes it again → back.svg
#     (tst_drawingcanvas excalidrawsVersionIsEditable);
#  4. excalidraw.com opens back.svg as Brulion wrote it.
# Needs the internet, so it is no ctest. Run inside `nix develop` (Qt plugins) with Chromium and
# Node >= 22 on PATH, after building tst_drawingcanvas:
#
#   tests/app/drawing/excalidraw-roundtrip.sh [<work dir>]
#   On the laptop: scripts/remote-build.sh -n drawing tst_drawingcanvas \
#     --run 'BRULION_BUILD_DIR=$BRULION_BUILD_DIR tests/app/drawing/excalidraw-roundtrip.sh'
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.."

build="${BRULION_BUILD_DIR:-build}"
test_bin="$build/tests/app/drawing/tst_drawingcanvas"
[[ -x "$test_bin" ]] || { echo "excalidraw-roundtrip: build tst_drawingcanvas first ($test_bin)" >&2; exit 1; }
work="${1:-$(mktemp -d)}"
mkdir -p "$work"
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software

BRULION_DRAWING_SHAPES_SAMPLE="$work/brulion.svg" "$test_bin" shapesSample >/dev/null
node tests/app/drawing/excalidraw-roundtrip.mjs "$work/brulion.svg" "$work/excalidraw.excalidraw" \
  --shot "$work/excalidraw-1.png"
BRULION_DRAWING_CHECK="$work/excalidraw.excalidraw" BRULION_DRAWING_CHECK_OUT="$work/back.svg" \
  "$test_bin" excalidrawsVersionIsEditable
node tests/app/drawing/excalidraw-roundtrip.mjs "$work/back.svg" "$work/again.excalidraw" \
  --shot "$work/excalidraw-2.png"
echo "excalidraw-roundtrip: ok ($work)"
