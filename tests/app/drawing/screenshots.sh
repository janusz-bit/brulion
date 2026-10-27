#!/usr/bin/env bash
# Screenshots of the drawing editor in the real app: a page of handwritten lecture notes (written
# by tst_drawingcanvas handwritingSample, BRULION_DRAWING_SAMPLE) in the editor, the page that
# embeds it, its picture view, an Excalidraw drawing with shapes (read-only) and a new empty drawing
# — × {desktop 1280×800, phone 412×915 --touch} × {light, dark} into
# tests/screenshots/drawing-<shot>-<size>-<theme>.png.
#
# Every run gets a fresh, temporary XDG home and notebook and the fixed clock --now.
#
#   tests/app/drawing/screenshots.sh                     # everything
#   tests/app/drawing/screenshots.sh editor page         # only these shots
#   SIZES=phone THEMES=dark tests/app/drawing/screenshots.sh editor
#   On the laptop: scripts/remote-build.sh -n drawing brulion tst_drawingcanvas \
#     --run 'BRULION_BUILD_DIR=$BRULION_BUILD_DIR tests/app/drawing/screenshots.sh' --pull tests/screenshots
#
# Run inside `nix develop` (Qt plugin and QML paths). One app instance at a time (lock).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.."

if [[ -z "${BRULION_GUI_LOCKED:-}" ]] && command -v flock >/dev/null 2>&1; then
  export BRULION_GUI_LOCKED=1
  exec flock /tmp/brulion-gui.lock "$0" "$@"
fi

build="${BRULION_BUILD_DIR:-build}"
bin="${BRULION_BIN:-$build/src/app/brulion}"
sample_test="${SAMPLE_TEST:-$build/tests/app/drawing/tst_drawingcanvas}"
out="${OUT_DIR:-tests/screenshots}"
locale="${LOCALE:-pl}"
now="${NOW:-2026-10-27 14:32}"
delay="${DELAY:-1600}"
read -r -a sizes <<< "${SIZES:-desktop phone}"
read -r -a themes <<< "${THEMES:-light dark}"

all_shots=(editor page picture readonly new style more paper)
if [[ $# -gt 0 ]]; then shots=("$@"); else shots=("${all_shots[@]}"); fi

for f in "$bin" "$sample_test"; do
  if [[ ! -x "$f" ]]; then
    echo "screenshots: $f not found — build brulion and tst_drawingcanvas first" >&2
    exit 1
  fi
done
mkdir -p "$out"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
vault="$work/Studia"
mkdir -p "$vault/assets"
BRULION_DRAWING_SAMPLE="$vault/assets/Rysunek 2026-10-27 14.32.svg" QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  "$sample_test" handwritingSample >/dev/null
cp tests/data/drawing/smiley_embedded_v2.svg "$vault/assets/Schemat z Excalidraw.svg"
cat > "$vault/Wykład z łączności.md" <<'MD'
# Wykład z łączności

- Modulacja amplitudy — notatki z tablicy:
  - ![[Rysunek 2026-10-27 14.32.svg]]
- Schemat od Natalii: ![[Schemat z Excalidraw.svg]]
- [ ] Przepisać wzory do zeszytu 📅 2026-10-29
MD
mkdir -p "$vault/.brulion"
printf '{"attachmentsFolder":"assets"}\n' > "$vault/.brulion/config.json"

shot_args() {
  case "$1" in
    editor) printf '%s\n' --route '{"view":"drawing","path":"assets/Rysunek 2026-10-27 14.32.svg"}' ;;
    page) printf '%s\n' --route '{"view":"page","path":"Wykład z łączności.md"}' ;;
    picture) printf '%s\n' --route '{"view":"page","path":"assets/Rysunek 2026-10-27 14.32.svg"}' ;;
    readonly) printf '%s\n' --route '{"view":"drawing","path":"assets/Schemat z Excalidraw.svg"}' ;;
    new) printf '%s\n' --route '{"view":"drawing","path":"assets/Pusty.svg"}' ;;
    style) printf '%s\n' --route '{"view":"drawing","path":"assets/Rysunek 2026-10-27 14.32.svg","open":"style"}' ;;
    more) printf '%s\n' --route '{"view":"drawing","path":"assets/Rysunek 2026-10-27 14.32.svg","open":"more"}' ;;
    paper) printf '%s\n' --route '{"view":"drawing","path":"assets/Rysunek 2026-10-27 14.32.svg","open":"paper"}' ;;
    *) return 1 ;;
  esac
}

suffix=""
[[ "$locale" != "pl" ]] && suffix+="-$locale"

failed=0
for shot in "${shots[@]}"; do
  mapfile -t args < <(shot_args "$shot") || true
  if [[ ${#args[@]} -eq 0 ]]; then
    echo "screenshots: unknown shot $shot" >&2
    exit 2
  fi
  for size in "${sizes[@]}"; do
    case "$size" in
      desktop) geometry=1280x800; extra=() ;;
      phone) geometry=412x915; extra=(--touch) ;;
      tablet) geometry=800x1180; extra=(--touch) ;;
      *) echo "screenshots: unknown size $size" >&2; exit 2 ;;
    esac
    for theme in "${themes[@]}"; do
      # The colours behind one swatch and "Więcej" exist on phones, the paper's button on wide windows.
      [[ ( "$shot" == style || "$shot" == more ) && "$size" == desktop ]] && continue
      [[ "$shot" == paper && "$size" != desktop ]] && continue
      file="$out/drawing-${shot}-${size}-${theme}${suffix}.png"
      home="$(mktemp -d)"
      copy="$home/Studia"
      cp -r "$vault" "$copy"
      # A new drawing: the paper empty (an empty file reads as a new drawing).
      [[ "$shot" == new ]] && : > "$copy/assets/Pusty.svg"
      if XDG_CONFIG_HOME="$home/config" XDG_DATA_HOME="$home/data" XDG_CACHE_HOME="$home/cache" XDG_STATE_HOME="$home/state" \
        QML_DISABLE_DISK_CACHE=1 QT_FORCE_STDERR_LOGGING=1 QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
        "$bin" --vault "$copy" "${args[@]}" --no-single-instance --size "$geometry" --theme "$theme" --locale "$locale" \
        --now "$now" --delay "$delay" "${extra[@]}" --screenshot "$file" \
        2> >(grep -v "^brulion: screenshot" >&2); then
        echo "$file"
      else
        echo "screenshots: FAILED $file" >&2
        failed=1
      fi
      rm -rf "$home"
    done
  done
done
exit "$failed"
