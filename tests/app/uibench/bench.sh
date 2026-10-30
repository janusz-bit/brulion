#!/usr/bin/env bash
# bench.sh — the UI benchmark, Brulion vs Obsidian (docs/UI.md "UI benchmark (Brulion vs
# Obsidian)"): the same notebook, window sizes, themes, fonts, clock and language for both apps;
# a screenshot and a layout dump of every screen of screens.json in both; the objective metrics.
#
#   tests/app/uibench/bench.sh --out DIR [--app brulion|obsidian|both] [--only id,…]
#       [--sizes desktop,phone] [--themes light,dark] [--reuse-obsidian DIR]
#       [--render offscreen|xcb-gl] [--dry-run]
#
# Environment: BRULION_BIN (default ${BRULION_BUILD_DIR:-build}/src/app/brulion),
#   UIMETRICS_BIN (default ${BRULION_BUILD_DIR:-build}/tests/app/uibench/brulion-uimetrics),
#   NOW (default screens.json "now"), OBSIDIAN_VERSION (1.14.4), OBSIDIAN_SHA256 (its AppImage's),
#   UIBENCH_CACHE (default ~/.cache/brulion-uibench: the AppImage, its extraction, the Calendar
#   plugin), BRULION_GIT_REV / BRULION_GIT_DIRTY (the manifest's revision when the tree has no
#   .git, e.g. on the laptop after scripts/remote-build.sh).
#
# Output in DIR: env/ (fonts.conf, the vault's hashes, the seed), vault-*/ (the notebook),
# shots/brulion/<id>-<size>-<theme>.{png,layout.json}, shots/obsidian/… (+ obsidian-manifest.json),
# metrics/<app>/<shot>.json, metrics.json, manifest.json, logs/. --render xcb-gl is the realism
# check: Brulion only, doc and daily on the desktop (unless --only/--sizes say otherwise), on Xvfb
# with OpenGL (Mesa llvmpipe) instead of offscreen + software, into shots/brulion-gl/,
# metrics-gl/, metrics-gl.json, manifest-gl.json. --dry-run prints every command and runs none
# (needs only node, to read screens.json). Exit 0 when every Brulion shot was taken (Obsidian's
# failures are recorded in the manifest), 1 otherwise, 2 for bad options.
#
# Laptop: scripts/remote-build.sh -n uibench brulion brulion-uimetrics \
#   --run 'tests/app/uibench/bench.sh --out $HOME/work/brulion-uibench-data/round-00'
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
# shellcheck source-path=SCRIPTDIR source=lib.sh
source "$here/lib.sh"

usage() {
  sed -n '6,8p' "${BASH_SOURCE[0]}" | sed 's/^# //' >&2
  exit 2
}

out=""; app=both; only=""; sizes=""; themes=""; reuse=""; render=offscreen; dry=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) out="$2"; shift 2 ;;
    --app) app="$2"; shift 2 ;;
    --only) only="$2"; shift 2 ;;
    --sizes) sizes="$2"; shift 2 ;;
    --themes) themes="$2"; shift 2 ;;
    --reuse-obsidian) reuse="$2"; shift 2 ;;
    --render) render="$2"; shift 2 ;;
    --dry-run) dry=1; shift ;;
    *) echo "bench: unknown option $1" >&2; usage ;;
  esac
done
[[ -n "$out" ]] || usage
[[ "$app" =~ ^(brulion|obsidian|both)$ ]] || usage
[[ "$render" =~ ^(offscreen|xcb-gl)$ ]] || usage
export UIBENCH_DRY=$dry

screens="$here/screens.json"
build="${BRULION_BUILD_DIR:-$repo/build}"
bin="${BRULION_BIN:-$build/src/app/brulion}"
uimetrics="${UIMETRICS_BIN:-$build/tests/app/uibench/brulion-uimetrics}"
obsidian_version="${OBSIDIAN_VERSION:-1.14.4}"
obsidian_sha256="${OBSIDIAN_SHA256:-6362ddbeeeebb7bbccb48fae009572cf2284ef92f5c919c1332aba48de6ffeaa}"
cache="${UIBENCH_CACHE:-$HOME/.cache/brulion-uibench}"
name="Przykładowy brulion"
xvfb=(xvfb-run -a -s "-screen 0 1920x1200x24 -dpi 96")

suffix=""
if [[ "$render" == xcb-gl ]]; then
  app=brulion
  only="${only:-doc,daily}"
  sizes="${sizes:-desktop}"
  suffix="-gl"
fi
brulion_dir="brulion$suffix"
metrics_dir="metrics$suffix"

# The shot list (also checks --only, --sizes and --themes).
shot_list="$(uibench_shots "$screens" "$only" "$sizes" "$themes")" || exit 2
all_shots="$(uibench_shots "$screens" "" "" "")"
now="${NOW:-$(node -e 'console.log(JSON.parse(require("node:fs").readFileSync(process.argv[1], "utf8")).now)' "$screens")}"
declare -A device_of=()
while IFS=$'\t' read -r _ size _ _ _ _ touch _ _; do
  [[ "$touch" == true ]] && device_of[$size]=phone || device_of[$size]=desktop
done <<< "$all_shots"

if [[ $dry == 0 ]]; then
  mkdir -p "$out/env" "$out/logs" "$out/tmp"
  out="$(cd "$out" && pwd)"
fi
started="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

# 1. Tools from the repository's nixpkgs (Xvfb, fontconfig's fc-match, Node 22, appimage-run).
tool_pkgs=(nixpkgs#xvfb-run nixpkgs#fontconfig.bin nixpkgs#nodejs_22)
[[ -e /etc/NIXOS ]] && tool_pkgs+=(nixpkgs#appimage-run)
if [[ $dry == 1 ]]; then
  uibench_run nix build --no-link --print-out-paths --inputs-from "path:$repo" "${tool_pkgs[@]}"
else
  mapfile -t tool_paths < <(nix build --no-link --print-out-paths --inputs-from "path:$repo" "${tool_pkgs[@]}")
  for p in "${tool_paths[@]}"; do
    [[ -d "$p/bin" ]] && PATH="$p/bin:$PATH"
  done
  export PATH
fi

# 2. Fonts: one fonts.conf for both apps (Noto Sans/Serif/Sans Mono, Noto Color Emoji, DejaVu).
export UIBENCH_FONTS="$out/env/fonts.conf"
if [[ $dry == 1 ]]; then
  uibench_run bash "$here/fonts.sh" "$out/env"
else
  bash "$here/fonts.sh" "$out/env" > "$out/env/font-dirs.txt"
fi

# 3. The notebook (reused when the overlay and the clock are the same).
vault_args=("$out")
[[ "$app" == brulion ]] && vault_args+=(--no-obsidian)
BRULION_BIN="$bin" NOW="$now" UIBENCH_CACHE="$cache" bash "$here/prepare-vault.sh" "${vault_args[@]}"

# 4. Brulion.
failures=()
if [[ "$app" != obsidian ]]; then
  [[ $dry == 1 ]] || mkdir -p "$out/shots/$brulion_dir" "$out/logs/$brulion_dir"
  while IFS=$'\t' read -r id size theme w h scale touch delay argstr; do
    [[ -n "$id" ]] || continue
    IFS=$'\x1f' read -r -a args <<< "$argstr"
    base="$out/shots/$brulion_dir/$id-$size-$theme"
    log="$out/logs/$brulion_dir/$id-$size-$theme.log"
    ok=0
    for attempt in 1 2; do
      if [[ $dry == 1 ]]; then
        if [[ "$render" == xcb-gl ]]; then printf '%q ' "${xvfb[@]}"; fi
        uibench_brulion_shot "$bin" "$out/vault-brulion/$name" "$out/tmp/brulion-home" "$base.png" \
          "$base.layout.json" "$w" "$h" "$scale" "$touch" "$theme" "$delay" "$render" "$now" "${args[@]}"
        ok=1
        break
      fi
      rm -f "$base.png" "$base.layout.json"
      if [[ "$render" == xcb-gl ]]; then
        # shellcheck disable=SC2016 # expanded by the inner bash
        "${xvfb[@]}" bash -c 'source "$1"; shift; uibench_brulion_shot "$@"' _ "$here/lib.sh" \
          "$bin" "$out/vault-brulion/$name" "$out/tmp/brulion-home" "$base.png" "$base.layout.json" \
          "$w" "$h" "$scale" "$touch" "$theme" "$delay" "$render" "$now" "${args[@]}" >"$log" 2>&1 || true
      else
        uibench_brulion_shot "$bin" "$out/vault-brulion/$name" "$out/tmp/brulion-home" "$base.png" \
          "$base.layout.json" "$w" "$h" "$scale" "$touch" "$theme" "$delay" "$render" "$now" "${args[@]}" \
          >"$log" 2>&1 || true
      fi
      if [[ -s "$base.png" && -s "$base.layout.json" ]]; then
        ok=1
        break
      fi
      echo "bench: brulion $id-$size-$theme failed (attempt $attempt): $(tail -n 3 "$log" | tr '\n' ' ')" >&2
    done
    if [[ $ok == 1 ]]; then
      [[ $dry == 1 ]] || echo "bench: brulion $id-$size-$theme" >&2
    else
      failures+=("brulion $id-$size-$theme")
    fi
  done <<< "$shot_list"
fi

# 5. Obsidian (the official AppImage, sha256 checked; under Xvfb, driven over CDP).
obsidian_status=0
launch=""
if [[ "$app" != brulion ]]; then
  if [[ -n "$reuse" ]]; then
    uibench_run mkdir -p "$out/shots/obsidian"
    uibench_run cp -a "$reuse/." "$out/shots/obsidian/"
  else
    appimage="$cache/Obsidian-$obsidian_version.AppImage"
    [[ "$cache" != *" "* ]] || { echo "bench: UIBENCH_CACHE must not contain spaces" >&2; exit 2; }
    if [[ $dry == 0 ]]; then
      mkdir -p "$cache"
      if [[ ! -s "$appimage" ]]; then
        curl -fL --retry 3 -o "$appimage.part" \
          "https://github.com/obsidianmd/obsidian-releases/releases/download/v$obsidian_version/Obsidian-$obsidian_version.AppImage"
        mv "$appimage.part" "$appimage"
      fi
      echo "$obsidian_sha256  $appimage" | sha256sum -c --quiet - || {
        echo "bench: $appimage does not match OBSIDIAN_SHA256" >&2
        exit 1
      }
    fi
    if [[ -e /etc/NIXOS ]]; then
      extracted="$cache/obsidian-$obsidian_version"
      [[ $dry == 1 || -d "$extracted" ]] || appimage-run -x "$extracted" "$appimage" >/dev/null
      launch="appimage-run -w $extracted --"
    else
      extracted="$cache/obsidian-$obsidian_version-root"
      if [[ $dry == 0 && ! -x "$extracted/obsidian" ]]; then
        chmod +x "$appimage"
        rm -rf "$cache/squashfs-root"
        (cd "$cache" && "$appimage" --appimage-extract >/dev/null)
        mv "$cache/squashfs-root" "$extracted"
      fi
      launch="$extracted/obsidian"
    fi
    obs_args=(--launch "$launch" --vault "$out/vault-obsidian/$name" --screens "$screens"
      --out "$out/shots/obsidian" --profile "$out/tmp/obsidian-profile" --now "$now" --lang pl
      --version "$obsidian_version" --fonts "$UIBENCH_FONTS")
    [[ -n "$only" ]] && obs_args+=(--only "$only")
    [[ -n "$sizes" ]] && obs_args+=(--sizes "$sizes")
    [[ -n "$themes" ]] && obs_args+=(--themes "$themes")
    if [[ $dry == 1 ]]; then
      uibench_run "${xvfb[@]}" node "$here/obsidian-shots.mjs" "${obs_args[@]}"
    else
      mkdir -p "$out/shots/obsidian"
      "${xvfb[@]}" node "$here/obsidian-shots.mjs" "${obs_args[@]}" 2> >(tee "$out/logs/obsidian.log" >&2) \
        || obsidian_status=$?
    fi
  fi
fi

# 6. Metrics: every shot of both apps, then the summary.
measure_failures=()
for dir in "$brulion_dir" obsidian; do
  [[ "$dir" == obsidian && -n "$suffix" ]] && continue
  if [[ $dry == 1 ]]; then
    uibench_run "$uimetrics" measure --png "$out/shots/$dir/ID-SIZE-THEME.png" --dump "$out/shots/$dir/ID-SIZE-THEME.layout.json" \
      --device desktop --screen ID --size SIZE --theme THEME --out "$out/$metrics_dir/$dir/ID-SIZE-THEME.json"
    continue
  fi
  [[ -d "$out/shots/$dir" ]] || continue
  mkdir -p "$out/$metrics_dir/$dir"
  for png in "$out/shots/$dir"/*.png; do
    [[ -e "$png" ]] || continue
    base="${png%.png}"
    shot="$(basename "$base")"
    theme="${shot##*-}"
    rest="${shot%-*}"
    size="${rest##*-}"
    id="${rest%-*}"
    [[ -s "$base.layout.json" ]] || continue
    if ! "$uimetrics" measure --png "$png" --dump "$base.layout.json" --device "${device_of[$size]:-desktop}" \
        --screen "$id" --size "$size" --theme "$theme" --out "$out/$metrics_dir/$dir/$shot.json"; then
      measure_failures+=("$dir $shot")
    fi
  done
done
uibench_run "$uimetrics" summarize --dir "$out/$metrics_dir" --out "$out/metrics$suffix.json" || measure_failures+=(summarize)

# 7. The manifest: what was compared, with which versions, fonts and notes.
if [[ $dry == 0 ]]; then
  git_rev="${BRULION_GIT_REV:-$(git -C "$repo" rev-parse HEAD 2>/dev/null || echo unknown)}"
  git_dirty="${BRULION_GIT_DIRTY:-$( [[ -n "$(git -C "$repo" status --porcelain 2>/dev/null)" ]] && echo true || echo false)}"
  build_type="$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "$(dirname "$(dirname "$(dirname "$bin")")")/CMakeCache.txt" 2>/dev/null || true)"
  fc_match=""
  for generic in sans-serif serif monospace; do
    fc_match+="$generic=$(FONTCONFIG_FILE="$UIBENCH_FONTS" fc-match -f '%{family[0]}' "$generic");"
  done
  printf '%s\n' "${failures[@]+"${failures[@]}"}" "${measure_failures[@]+"${measure_failures[@]}"}" > "$out/tmp/failures.txt"
  node -e '
const fs = require("node:fs"), crypto = require("node:crypto"), path = require("node:path");
const [out, suffix, started, now, gitRev, dirty, buildType, render, bin, version, sha, launch, fcMatch, screens, app] = process.argv.slice(1);
const read = (f) => { try { return fs.readFileSync(f, "utf8"); } catch { return null; } };
const sha256 = (s) => crypto.createHash("sha256").update(s ?? "").digest("hex");
const obs = JSON.parse(read(path.join(out, "shots/obsidian/obsidian-manifest.json")) ?? "null");
const shots = (dir) => { try { return fs.readdirSync(path.join(out, "shots", dir)).filter((f) => f.endsWith(".png")).sort(); } catch { return []; } };
const vault = (read(path.join(out, "env/vault.sha256")) ?? "").trim().split("\n").filter(Boolean)
  .map((l) => ({ sha256: l.slice(0, 64), file: l.slice(66) }));
const fonts = (read(path.join(out, "env/font-dirs.txt")) ?? "").trim().split("\n").filter(Boolean);
const overlay = (read(path.join(out, "env/overlay.sha256")) ?? "").trim();
const screensText = read(screens);
const m = {
  schema: 1, date: started, finished: new Date().toISOString(), now, app,
  brulion: { gitRev, dirty: dirty === "true", buildType, render, bin },
  obsidian: obs ? { version, sha256: sha, launch, environment: obs.environment, warnings: obs.warnings, failures: obs.failures } : null,
  fonts: { dirs: fonts, fcMatch, conf: read(path.join(out, "env/fonts.conf")) },
  vault: { overlay, files: vault },
  screensHash: sha256(screensText),
  // Obsidian shots stay valid while this is the same (--reuse-obsidian).
  benchConfigHash: sha256([screensText, overlay, fonts.join("\n"), version, now].join("\n---\n")),
  shots: { brulion: shots("brulion" + suffix), obsidian: suffix ? [] : shots("obsidian") },
  failures: (read(path.join(out, "tmp/failures.txt")) ?? "").split("\n").filter(Boolean),
};
fs.writeFileSync(path.join(out, "manifest" + suffix + ".json"), JSON.stringify(m, null, 2) + "\n");
' "$out" "$suffix" "$started" "$now" "$git_rev" "$git_dirty" "$build_type" "$render" "$bin" \
    "$obsidian_version" "$obsidian_sha256" "$launch" "$fc_match" "$screens" "$app"
  rm -rf "$out/tmp"
  echo "bench: $out/manifest$suffix.json" >&2
fi

status=0
if [[ ${#failures[@]} -gt 0 ]]; then
  echo "bench: failed shots: ${failures[*]}" >&2
  status=1
fi
if [[ ${#measure_failures[@]} -gt 0 ]]; then
  echo "bench: metrics failed for: ${measure_failures[*]}" >&2
  status=1
fi
[[ $obsidian_status == 0 ]] || echo "bench: the Obsidian driver exited with $obsidian_status (see the manifest)" >&2
exit $status
