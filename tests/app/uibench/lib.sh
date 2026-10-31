# shellcheck shell=bash
# lib.sh — helpers shared by the UI benchmark's scripts (bench.sh, prepare-vault.sh, lint.sh;
# docs/UI.md "UI benchmark (Brulion vs Obsidian)"). Sourced, not run.
#
#   uibench_run CMD…        runs CMD, or prints it (shell-quoted) when UIBENCH_DRY=1
#   uibench_shots FILE ONLY SIZES THEMES
#                           the shots of screens.json FILE (ONLY/SIZES/THEMES: comma lists, ""
#                           = all), one per line: id, size, theme, width, height, scale, touch,
#                           delayMs, the Brulion arguments joined by \x1f — tab-separated. Exit 2
#                           on an unknown screen, size or theme. Needs node.
#   uibench_brulion_shot BIN VAULT HOME PNG DUMP W H SCALE TOUCH THEME DELAY RENDER NOW [ARGS…]
#                           one Brulion screenshot + layout dump: a fresh home HOME (XDG folders)
#                           with a fresh copy of the vault folder VAULT, offscreen + software
#                           (RENDER offscreen) or on the X server of $DISPLAY with OpenGL (RENDER
#                           xcb-gl). FONTCONFIG_FILE is passed on when UIBENCH_FONTS is set.
#   uibench_config_hash SCREENS OUT DRIVER VERSION NOW
#                           the bench configuration's hash (the manifest's benchConfigHash; Obsidian
#                           shots are reused only under the same one): screens.json without
#                           Brulion's arguments, OUT/env/{vault,overlay}.sha256 (the notes),
#                           OUT/env/fonts.conf (without its cache folder), the Obsidian driver
#                           DRIVER, Obsidian's VERSION and the clock NOW.

uibench_run() {
  if [[ "${UIBENCH_DRY:-0}" == 1 ]]; then
    printf '%q ' "$@"
    printf '\n'
  else
    "$@"
  fi
}

uibench_shots() {
  command -v node >/dev/null 2>&1 || { echo "uibench: node is needed to read screens.json" >&2; return 3; }
  node -e '
const fs = require("node:fs");
const [file, only, sizes, themes] = process.argv.slice(1);
const m = JSON.parse(fs.readFileSync(file, "utf8"));
const list = (s) => s ? s.split(",").filter(Boolean) : null;
const o = list(only), z = list(sizes), t = list(themes);
const fail = (msg) => { console.error("uibench: " + msg); process.exit(2); };
for (const id of o ?? []) if (!m.screens.some((s) => s.id === id)) fail("unknown screen " + id);
for (const s of z ?? []) if (!m.sizes[s]) fail("unknown size " + s);
for (const s of t ?? []) if (!m.themes.includes(s)) fail("unknown theme " + s);
const out = [];
for (const s of m.screens) {
  if (o && !o.includes(s.id)) continue;
  for (const size of s.sizes) {
    if (z && !z.includes(size)) continue;
    const g = m.sizes[size];
    for (const theme of m.themes) {
      if (t && !t.includes(theme)) continue;
      for (const a of s.brulion.args) if (/[\t\x1f\n]/.test(a)) fail(s.id + ": an argument with a tab or a newline");
      out.push([s.id, size, theme, g.width, g.height, g.scale, g.touch, s.brulion.delayMs, s.brulion.args.join("\x1f")].join("\t"));
    }
  }
}
process.stdout.write(out.join("\n") + (out.length ? "\n" : ""));
' "$@"
}

uibench_brulion_shot() {
  local bin=$1 vault_src=$2 home=$3 png=$4 dump=$5 w=$6 h=$7 scale=$8 touch=$9 theme=${10} delay=${11}
  local render=${12} now=${13}
  shift 13
  local vault="$home/Przykładowy brulion"
  if [[ "${UIBENCH_DRY:-0}" != 1 ]]; then
    rm -rf "$home"
    mkdir -p "$home"
    cp -a "$vault_src" "$vault"
  fi
  local -a env=(TZ=Europe/Warsaw QML_DISABLE_DISK_CACHE=1 QT_FORCE_STDERR_LOGGING=1 HOME="$home"
    XDG_CONFIG_HOME="$home/config" XDG_DATA_HOME="$home/data" XDG_CACHE_HOME="$home/cache"
    XDG_STATE_HOME="$home/state")
  [[ -n "${UIBENCH_FONTS:-}" ]] && env+=(FONTCONFIG_FILE="$UIBENCH_FONTS")
  if [[ "$render" == xcb-gl ]]; then
    env+=(QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 QSG_INFO=1)
  else
    env+=(QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software)
  fi
  local -a extra=()
  [[ "$touch" == true ]] && extra+=(--touch)
  uibench_run env "${env[@]}" "$bin" --vault "$vault" "$@" --no-single-instance --size "${w}x${h}" \
    --scale "$scale" "${extra[@]}" --theme "$theme" --locale pl --now "$now" --delay "$delay" \
    --screenshot "$png" --dump-layout "$dump"
}

uibench_config_hash() {
  node -e '
const fs = require("node:fs"), crypto = require("node:crypto"), path = require("node:path");
const [screens, out, driver, version, now] = process.argv.slice(1);
const read = (f) => { try { return fs.readFileSync(f, "utf8"); } catch { return ""; } };
const m = JSON.parse(read(screens));
const parts = [
  JSON.stringify({ ...m, screens: m.screens.map(({ brulion, ...rest }) => rest) }),
  read(path.join(out, "env/vault.sha256")),
  read(path.join(out, "env/overlay.sha256")),
  read(path.join(out, "env/fonts.conf")).split("\n").filter((l) => !l.includes("<cachedir>")).join("\n"),
  crypto.createHash("sha256").update(read(driver)).digest("hex"),
  version,
  now,
];
console.log(crypto.createHash("sha256").update(parts.join("\n---\n")).digest("hex"));
' "$@"
}
