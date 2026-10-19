#!/usr/bin/env bash
# Screenshots of the content views in the real app with the sample notebook: journals, pages
# (with the side panel, the rename and export dialogs, unlinked mentions), all pages, tags, a
# tag, search, trash and history — × {desktop 1280×800, phone 412×915 --touch} × {light, dark}
# — into tests/screenshots/content-<shot>-<size>-<theme>.png. QML warnings of the run are
# printed. Fresh temporary XDG homes and the fixed clock --now keep the images reproducible.
#
#   tests/app/content/screenshots.sh                     # everything
#   tests/app/content/screenshots.sh journals page       # only these shots
#   SIZES=phone THEMES=dark tests/app/content/screenshots.sh search
#   On the laptop: scripts/remote-build.sh -n content brulion \
#     --run 'BRULION_BIN=$BRULION_BUILD_DIR/src/app/brulion tests/app/content/screenshots.sh' \
#     --pull tests/screenshots
#
# Run inside `nix develop` (Qt plugin and QML paths). One app instance at a time (lock).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.."

if [[ -z "${BRULION_GUI_LOCKED:-}" ]] && command -v flock >/dev/null 2>&1; then
  export BRULION_GUI_LOCKED=1
  exec flock /tmp/brulion-gui.lock "$0" "$@"
fi

bin="${BRULION_BIN:-build/src/app/brulion}"
out="${OUT_DIR:-tests/screenshots}"
locale="${LOCALE:-pl}"
now="${NOW:-2026-10-14 10:00}"
delay="${DELAY:-1800}"
read -r -a sizes <<< "${SIZES:-desktop phone}"
read -r -a themes <<< "${THEMES:-light dark}"

declare -A shot_route=(
  [journals]='journals'
  [journals-day]='{"view":"journals","date":"2026-10-07"}'
  [journals-missed]='journals'
  [journals-jump]='{"view":"journals","open":"jump"}'
  [journals-write]='{"view":"journals","open":"write"}'
  [journal]='{"view":"journal","date":"2026-10-13"}'
  [journal-new]='{"view":"journal","date":"2026-10-15"}'
  [page]='{"view":"page","path":"Projekty/Remont mieszkania.md"}'
  [page-refs]='{"view":"page","path":"Książki 2026.md","open":"unlinked"}'
  [page-rename]='{"view":"page","path":"Projekty/Remont mieszkania.md","open":"rename"}'
  [page-export]='{"view":"page","path":"Projekty/Remont mieszkania.md","open":"export"}'
  [side-links]='{"view":"page","path":"Projekty/Remont mieszkania.md","open":"links"}'
  [side-outline]='{"view":"page","path":"Witaj w Brulionie.md","open":"outline"}'
  [side-calendar]='{"view":"journal","date":"2026-10-13","open":"calendar"}'
  [page-missing]='{"view":"page","path":"Pomysły na prezenty.md"}'
  [allpages]='allPages'
  [allpages-modified]='{"view":"allPages","sort":"modified"}'
  [tags]='tags'
  [tag]='{"view":"tag","tag":"pomysł"}'
  [search]='{"view":"search","query":"remont"}'
  [search-empty]='search'
  [trash]='trash'
  [trash-full]='trash'
  [empty-journals]='journals'
  [empty-allpages]='allPages'
  [empty-tags]='tags'
  [empty-search]='{"view":"search","query":"żółw"}'
  [history]='{"view":"history","path":"Witaj w Brulionie.md"}'
)
# A first run in the same home before the shot (the history needs an edit to show versions:
# PageScript "suggest" types into the guide page, which saves it).
declare -A shot_prep=(
  [history]='--editor-script suggest'
  [journals-missed]='--route journals'
  [journals-write]='--route journals'
)
# Clocks other than $now: the prep run seeds the notebook on the 13th, the shot opens it on the
# 15th — the reminders of the 14th were missed while Brulion was closed (and the 15th has no
# journal yet: the "start writing" prompt).
declare -A prep_now=(
  [journals-missed]='2026-10-13 10:00'
  [journals-write]='2026-10-13 10:00'
)
declare -A shot_now=(
  [journals-missed]='2026-10-15 09:00'
  [journals-write]='2026-10-15 09:00'
)
# Shots on a notebook folder of their own with things in its trash (as if deleted on another
# device: no local metadata), seeded with the sample notes around them.
declare -A shot_trash=(
  [trash-full]=1
)
# Shots on a new, empty notebook (no sample notes).
declare -A shot_empty=(
  [empty-journals]=1 [empty-allpages]=1 [empty-tags]=1 [empty-search]=1
)
all_shots=(journals journals-day journals-missed journals-jump journals-write journal journal-new page page-refs page-rename page-export page-missing
  side-links side-outline side-calendar
  allpages allpages-modified tags tag search search-empty trash trash-full history
  empty-journals empty-allpages empty-tags empty-search)
if [[ $# -gt 0 ]]; then shots=("$@"); else shots=("${all_shots[@]}"); fi

[[ -x "$bin" ]] || { echo "screenshots: $bin not found — build first" >&2; exit 1; }
mkdir -p "$out"
suffix=""
[[ "$locale" != "pl" ]] && suffix+="-$locale"

failed=0
for shot in "${shots[@]}"; do
  [[ -n "${shot_route[$shot]:-}" ]] || { echo "screenshots: unknown shot $shot" >&2; exit 2; }
  route="${shot_route[$shot]}"
  for size in "${sizes[@]}"; do
    case "$size" in
      desktop) geometry=1280x800; extra=() ;;
      phone) geometry=412x915; extra=(--touch) ;;
      desktop-tall) geometry=1280x2400; extra=() ;;
      phone-tall) geometry=412x2800; extra=(--touch) ;;
      *) echo "screenshots: unknown size $size" >&2; exit 2 ;;
    esac
    # The side panel only exists on wide windows.
    [[ "$shot" == side-* && "$size" == phone* ]] && continue
    for theme in "${themes[@]}"; do
      file="$out/content-${shot}-${size}-${theme}${suffix}.png"
      home="$(mktemp -d)"
      vault_args=(--demo)
      if [[ -n "${shot_empty[$shot]:-}" ]]; then
        mkdir -p "$home/empty"
        vault_args=(--vault "$home/empty")
      fi
      if [[ -n "${shot_trash[$shot]:-}" ]]; then
        mkdir -p "$home/vault/.trash/Stare projekty" "$home/vault/.trash/journals"
        printf -- '- [ ] Pomalować płot\n- Kupić farbę #remont\n' > "$home/vault/.trash/Stare projekty/Ogród.md"
        printf -- '- Notatki ze spotkania\n' > "$home/vault/.trash/Stare projekty/Spotkanie z Anią.md"
        printf -- '# Zażółć gęślą jaźń\n\nStara notatka z bardzo długim tytułem, żeby sprawdzić zawijanie.\n' \
          > "$home/vault/.trash/Bardzo długi tytuł notatki o przygotowaniach do przeprowadzki.md"
        printf -- '- dzień\n' > "$home/vault/.trash/journals/2026-09-30.md"
        vault_args=(--demo --vault "$home/vault")
      fi
      if [[ -n "${shot_prep[$shot]:-}" ]]; then
        read -r -a prep <<< "${shot_prep[$shot]}"
        XDG_CONFIG_HOME="$home/config" XDG_DATA_HOME="$home/data" XDG_CACHE_HOME="$home/cache" XDG_STATE_HOME="$home/state" \
          QML_DISABLE_DISK_CACHE=1 QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
          "$bin" --demo --no-single-instance "${prep[@]}" --size "$geometry" --locale "$locale" --now "${prep_now[$shot]:-$now}" \
          --delay 2600 "${extra[@]}" --screenshot "$home/prep.png" >/dev/null 2>&1 || true
      fi
      if XDG_CONFIG_HOME="$home/config" XDG_DATA_HOME="$home/data" XDG_CACHE_HOME="$home/cache" XDG_STATE_HOME="$home/state" \
        QML_DISABLE_DISK_CACHE=1 QT_FORCE_STDERR_LOGGING=1 QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
        "$bin" "${vault_args[@]}" --no-single-instance --route "$route" --size "$geometry" --theme "$theme" --locale "$locale" \
        --now "${shot_now[$shot]:-$now}" --delay "$delay" "${extra[@]}" --screenshot "$file" \
        2> >(grep -v "^brulion: screenshot" | grep -v "propertyCache.append: Final member" >&2); then
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
