#!/usr/bin/env bash
# prepare-vault.sh <out-dir> [--no-obsidian] — the UI benchmark's notebook (docs/UI.md "UI
# benchmark (Brulion vs Obsidian)"): Brulion's sample notebook seeded at the bench's clock, with
# the neutral overlay of tests/app/uibench/vault/ copied over it:
#
#   <out>/vault-pristine/Przykładowy brulion   the notebook as seeded + the overlay
#   <out>/vault-brulion/Przykładowy brulion    a copy for Brulion's shots (each shot copies it again)
#   <out>/vault-obsidian/Przykładowy brulion   a copy without .brulion/, with .obsidian/ (daily
#                                              notes in journals/, the Calendar plugin 1.5.10)
#   <out>/env/vault.sha256                     sha256 of every note (both apps start from them)
#   <out>/env/overlay.sha256                   the overlay's hash (a pristine notebook of another
#                                              overlay is never reused: the script refuses)
#
# Environment: BRULION_BIN (default build/src/app/brulion), NOW (default "2026-10-14 10:00"),
# UIBENCH_CACHE (the Calendar plugin's files; default ~/.cache/brulion-uibench), UIBENCH_FONTS
# (fonts.conf for the seeding run), UIBENCH_DRY=1 (print the commands only).
# --no-obsidian: no Obsidian copy (lint.sh; no download).
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
# shellcheck source=lib.sh
source "$here/lib.sh"

out="${1:?usage: prepare-vault.sh <out-dir> [--no-obsidian]}"
obsidian=1
[[ "${2:-}" == --no-obsidian ]] && obsidian=0
bin="${BRULION_BIN:-${BRULION_BUILD_DIR:-$repo/build}/src/app/brulion}"
now="${NOW:-2026-10-14 10:00}"
cache="${UIBENCH_CACHE:-$HOME/.cache/brulion-uibench}"
name="Przykładowy brulion"
pristine="$out/vault-pristine/$name"

overlay_hash="$( (cd "$here" && find vault obsidian -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum
                  printf 'now %s\n' "$now") | sha256sum | cut -d' ' -f1)"

if [[ "${UIBENCH_DRY:-0}" == 1 ]]; then
  uibench_run env TZ=Europe/Warsaw QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software "$bin" --demo \
    --vault "$pristine" --no-single-instance --size 1280x800 --locale pl --now "$now" --delay 600 \
    --screenshot "$out/env/seed.png"
  uibench_run cp -R "$here/vault/." "$pristine/"
else
  mkdir -p "$out/env"
  if [[ -d "$pristine" ]]; then
    if [[ "$(cat "$out/env/overlay.sha256" 2>/dev/null)" != "$overlay_hash" ]]; then
      echo "prepare-vault: $pristine was made from another overlay or clock; remove $out/vault-pristine first" >&2
      exit 1
    fi
    echo "prepare-vault: reusing $pristine" >&2
  else
    # 1. Seed: the sample notebook as the app writes it (--demo --vault), in a private home.
    seed_home="$(mktemp -d "$out/env/seed-home.XXXXXX")"
    mkdir -p "$out/vault-pristine"
    env_args=(TZ=Europe/Warsaw QML_DISABLE_DISK_CACHE=1 QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software
      HOME="$seed_home" XDG_CONFIG_HOME="$seed_home/config" XDG_DATA_HOME="$seed_home/data"
      XDG_CACHE_HOME="$seed_home/cache" XDG_STATE_HOME="$seed_home/state")
    [[ -n "${UIBENCH_FONTS:-}" ]] && env_args+=(FONTCONFIG_FILE="$UIBENCH_FONTS")
    env "${env_args[@]}" "$bin" --demo --vault "$pristine" --no-single-instance --size 1280x800 --locale pl \
      --now "$now" --delay 600 --screenshot "$out/env/seed.png" >"$out/env/seed.log" 2>&1 || {
        echo "prepare-vault: seeding failed:" >&2
        cat "$out/env/seed.log" >&2
        exit 1
      }
    rm -rf "$seed_home"
    [[ -f "$pristine/journals/${now%% *}.md" ]] || { echo "prepare-vault: no journal for ${now%% *} in the seed" >&2; exit 1; }
    # 2. The overlay: neutral notes both apps render (a document, an outline, properties).
    cp -R "$here/vault/." "$pristine/"
    echo "$overlay_hash" > "$out/env/overlay.sha256"
  fi
  # 3. The notes' hashes (both apps start from byte-identical notes).
  (cd "$pristine" && find . -type f ! -path './.brulion/*' ! -path './.obsidian/*' -print0 | LC_ALL=C sort -z \
    | xargs -0 sha256sum) > "$out/env/vault.sha256"
fi

# 4. The copies.
uibench_run rm -rf "$out/vault-brulion" "$out/vault-obsidian"
uibench_run mkdir -p "$out/vault-brulion" "$out/vault-obsidian"
uibench_run cp -a "$pristine" "$out/vault-brulion/"
if [[ $obsidian == 1 ]]; then
  uibench_run cp -a "$pristine" "$out/vault-obsidian/"
  uibench_run rm -rf "$out/vault-obsidian/$name/.brulion"
  plugin="$cache/calendar-1.5.10"
  if [[ "${UIBENCH_DRY:-0}" != 1 ]]; then
    mkdir -p "$plugin"
    for f in main.js manifest.json; do
      [[ -s "$plugin/$f" ]] || curl -fsSL --retry 3 -o "$plugin/$f" \
        "https://github.com/liamcain/obsidian-calendar-plugin/releases/download/1.5.10/$f"
    done
    (cd "$plugin" && grep -v '^#' "$here/obsidian/plugins.sha256" | sha256sum -c --quiet -) || {
      echo "prepare-vault: the Calendar plugin's files do not match obsidian/plugins.sha256" >&2
      exit 1
    }
  fi
  dot="$out/vault-obsidian/$name/.obsidian"
  uibench_run mkdir -p "$dot/plugins/calendar"
  uibench_run cp "$here/obsidian/daily-notes.json" "$here/obsidian/community-plugins.json" "$dot/"
  uibench_run cp "$plugin/main.js" "$plugin/manifest.json" "$dot/plugins/calendar/"
fi
