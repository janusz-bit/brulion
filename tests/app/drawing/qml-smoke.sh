#!/usr/bin/env bash
# QML smoke test of the drawing editor (ctest tst_drawing_qml): the route "drawing" on a drawing
# (Excalidraw freehand), an Excalidraw drawing with shapes (read-only), a picture that is no
# drawing, a missing file and an empty one (a new drawing), the drawing's background menu, the page
# embedding them and the picture view of a drawing, in the real app at desktop and phone sizes in
# the light and the dark theme, headless; fails when the app does not start or QML reports a
# warning or error from the drawing editor's files, the embeds or the picture view.
#
#   tests/app/drawing/qml-smoke.sh <brulion binary> <scratch dir> <tests/data/drawing>
set -uo pipefail
bin="$1"
work="$2"
data="$3"
rm -rf "$work"
mkdir -p "$work"

vault="$work/vault"
mkdir -p "$vault/assets" "$vault/.brulion"
printf '{"attachmentsFolder":"assets"}\n' > "$vault/.brulion/config.json"
cp "$data/excalidraw_freedraw.svg" "$vault/assets/Rysunek ćwiczenia.svg"
cp "$data/excalidraw_mixed.svg" "$vault/assets/Schemat.svg"
printf '<svg xmlns="http://www.w3.org/2000/svg" width="40" height="40"><rect width="40" height="40" fill="#e2574c"/></svg>' \
  > "$vault/assets/logo.svg"
: > "$vault/assets/Nowy.svg"
printf -- '- Ćwiczenie: ![[Rysunek ćwiczenia.svg]]\n- ![[Schemat.svg]]\n- ![[logo.svg]]\n' > "$vault/Zajęcia.md"

routes=(
  '{"view":"drawing","path":"assets/Rysunek ćwiczenia.svg"}'
  '{"view":"drawing","path":"assets/Schemat.svg"}'
  '{"view":"drawing","path":"assets/logo.svg"}'
  '{"view":"drawing","path":"assets/Brak.svg"}'
  '{"view":"drawing","path":"assets/Nowy.svg"}'
  '{"view":"drawing","path":"assets/Rysunek ćwiczenia.svg","open":"theme"}'
  '{"view":"page","path":"Zajęcia.md"}'
  '{"view":"page","path":"assets/Rysunek ćwiczenia.svg"}'
)
pattern='qml/(drawing/|editor/EmbedView|content/ContentPictureView|shell/(ShellRouteView|AppShell))'

failed=0
for theme in light dark; do
  for size in desktop phone; do
    if [[ "$size" == desktop ]]; then geometry=(--size 1280x800); else geometry=(--size 412x915 --touch); fi
    for route in "${routes[@]}"; do
      home="$work/home"
      rm -rf "$home"
      copy="$work/copy"
      rm -rf "$copy"
      cp -r "$vault" "$copy"
      log="$work/log.txt"
      if ! XDG_CONFIG_HOME="$home/config" XDG_DATA_HOME="$home/data" XDG_CACHE_HOME="$home/cache" XDG_STATE_HOME="$home/state" \
        "$bin" --vault "$copy" --route "$route" --no-single-instance "${geometry[@]}" --theme "$theme" --now "2026-10-27 14:32" \
        --locale pl --delay 1200 --screenshot "$work/shot.png" 2> "$log"; then
        echo "FAIL: the app did not run for $route ($size, $theme)"
        cat "$log"
        failed=$((failed + 1))
        continue
      fi
      if grep -E "$pattern" "$log"; then
        echo "FAIL: QML warnings for $route ($size, $theme)"
        failed=$((failed + 1))
      else
        echo "ok: $route ($size, $theme)"
      fi
      # Looking is not changing: no file was written.
      if ! diff -r "$vault" "$copy" >/dev/null; then
        echo "FAIL: $route ($size, $theme) changed the notebook"
        diff -r "$vault" "$copy" | head -5
        failed=$((failed + 1))
      fi
    done
  done
done
exit "$failed"
