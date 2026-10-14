#!/usr/bin/env bash
# Browser version (Qt for WebAssembly) -> dist-wasm/: a static site (offline PWA) for GitHub
# Pages or any web server. Idempotent: re-running reuses the build directory and the
# toolchains, and rebuilds only what changed. See docs/WEB.md.
#
#   scripts/wasm-build.sh               # configure + build + assemble dist-wasm/
#   scripts/wasm-build.sh --setup       # first install emsdk + Qt for WebAssembly if missing
#   scripts/wasm-build.sh --assemble    # only re-assemble dist-wasm/ from an existing build
#   scripts/wasm-build.sh --clean       # start with an empty build directory
#
# Environment (defaults in brackets):
#   BRULION_CACHE  toolchain cache [${XDG_CACHE_HOME:-~/.cache}]
#   EMSDK          emsdk checkout [$BRULION_CACHE/brulion-emsdk/emsdk]
#   QT_ROOT        aqt install root [$BRULION_CACHE/brulion-qt]
#   QT_WASM        Qt for WebAssembly [$QT_ROOT/6.11.2/wasm_singlethread]
#   QT_HOST        desktop Qt of the same version, for moc/rcc/qmlcachegen/lrelease
#                  [$QT_ROOT/6.11.2/gcc_64]
#   BUILD_DIR [build-wasm]  DIST_DIR [dist-wasm]  BUILD_TYPE [MinSizeRel]  JOBS [nproc]
#
# Needs cmake >= 3.24, ninja, python3, git; --setup also uvx (or aqt) for aqtinstall. On NixOS
# the script enters a `nix shell` with these by itself (and needs programs.nix-ld).
# Heavy: on the Raspberry Pi dev machine run it on the laptop (laptop-run) or in CI.
set -euo pipefail
script=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")
cd "$(dirname "$script")/.."
root=$PWD

# Qt 6.11 requires exactly this Emscripten (QT_EMCC_RECOMMENDED_VERSION in Qt's CMake files;
# doc.qt.io/qt-6.11/wasm.html). Bump both together.
QT_VERSION=6.11.2
EMSDK_VERSION=4.0.7
AQT_VERSION=3.3.0

cache=${BRULION_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}}
EMSDK=${EMSDK:-$cache/brulion-emsdk/emsdk}
QT_ROOT=${QT_ROOT:-$cache/brulion-qt}
QT_WASM=${QT_WASM:-$QT_ROOT/$QT_VERSION/wasm_singlethread}
QT_HOST=${QT_HOST:-$QT_ROOT/$QT_VERSION/gcc_64}
BUILD_DIR=${BUILD_DIR:-build-wasm}
DIST_DIR=${DIST_DIR:-dist-wasm}
BUILD_TYPE=${BUILD_TYPE:-MinSizeRel}
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}

do_setup=0
do_build=1
do_clean=0
for arg in "$@"; do
  case "$arg" in
    --setup) do_setup=1 ;;
    --assemble) do_build=0 ;;
    --clean) do_clean=1 ;;
    -h | --help) awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$script"; exit 0 ;;
    *) echo "wasm-build: unknown option $arg" >&2; exit 2 ;;
  esac
done

log() { printf '\033[1m[wasm-build]\033[0m %s\n' "$*" >&2; }
die() { log "error: $*"; exit 1; }

sha256() {
  if command -v sha256sum >/dev/null; then sha256sum | cut -c1-16; else shasum -a 256 | cut -c1-16; fi
}

aqt() {
  if command -v aqt >/dev/null; then
    command aqt "$@"
  elif command -v uvx >/dev/null; then
    uvx --from "aqtinstall==$AQT_VERSION" aqt "$@"
  else
    die "aqt not found: install uv (uvx) or aqtinstall==$AQT_VERSION"
  fi
}

setup() {
  if [[ ! -x "$EMSDK/emsdk" ]]; then
    log "cloning emsdk into $EMSDK"
    mkdir -p "$(dirname "$EMSDK")"
    git clone --quiet https://github.com/emscripten-core/emsdk.git "$EMSDK"
  fi
  log "emsdk install + activate $EMSDK_VERSION (quick when already installed)"
  "$EMSDK/emsdk" install "$EMSDK_VERSION" >&2
  "$EMSDK/emsdk" activate "$EMSDK_VERSION" >&2

  if [[ ! -f "$QT_WASM/lib/cmake/Qt6/qt.toolchain.cmake" ]]; then
    log "installing Qt $QT_VERSION wasm_singlethread into $QT_ROOT"
    aqt install-qt all_os wasm "$QT_VERSION" wasm_singlethread -O "$QT_ROOT" >&2
  fi
  if [[ ! -d "$QT_HOST/lib/cmake/Qt6" ]]; then
    log "installing host Qt $QT_VERSION linux_gcc_64 into $QT_ROOT"
    aqt install-qt linux desktop "$QT_VERSION" linux_gcc_64 -O "$QT_ROOT" >&2
  fi
}

# The Pi (3.7 GB) must not link a Qt WebAssembly app (wasm-ld + wasm-opt need several GB).
if [[ $do_build == 1 && -z "${BRULION_FORCE_LOCAL:-}" && -r /proc/meminfo ]]; then
  mem_kb=$(awk '/^MemTotal:/ {print $2}' /proc/meminfo)
  if ((mem_kb < 6 * 1024 * 1024)); then
    die "only $((mem_kb / 1024)) MB RAM: build on the laptop (laptop-run) or in CI; BRULION_FORCE_LOCAL=1 overrides"
  fi
fi

# NixOS: cmake/ninja/ccache/python from nixpkgs; emsdk's clang/node and aqt's Qt host tools are
# prebuilt binaries that run through nix-ld — Qt's host tools also need glib, krb5 and brotli.
if [[ -e /etc/NIXOS && -z ${BRULION_WASM_IN_NIX_SHELL:-} && ($do_build == 1 || $do_setup == 1) ]]; then
  [[ -e /lib64/ld-linux-x86-64.so.2 ]] ||
    die "NixOS without nix-ld: emsdk and Qt's host tools are prebuilt binaries (enable programs.nix-ld)"
  log "NixOS: entering nix shell (cmake, ninja, ccache, python3) with nix-ld libraries"
  libs=$(nix build --no-link --print-out-paths nixpkgs#glib.out nixpkgs#krb5.lib \
    nixpkgs#brotli.lib nixpkgs#keyutils.lib) || die "nix build of the host-tool libraries failed"
  ld_path=
  for p in $libs; do ld_path+="$p/lib:"; done
  export NIX_LD_LIBRARY_PATH="${ld_path}${NIX_LD_LIBRARY_PATH:-/run/current-system/sw/share/nix-ld/lib}"
  export BRULION_WASM_IN_NIX_SHELL=1
  exec nix shell nixpkgs#cmake nixpkgs#ninja nixpkgs#ccache nixpkgs#python3 nixpkgs#git \
    nixpkgs#brotli --command bash "$script" "$@"
fi

if [[ $do_setup == 1 ]]; then
  setup
fi

if [[ $do_build == 1 ]]; then
  [[ -f "$EMSDK/emsdk_env.sh" ]] || die "emsdk not found at $EMSDK (run with --setup or set EMSDK)"
  toolchain=$QT_WASM/lib/cmake/Qt6/qt.toolchain.cmake
  [[ -f "$toolchain" ]] || die "Qt for WebAssembly not found at $QT_WASM (--setup or QT_WASM)"
  [[ -d "$QT_HOST/lib/cmake/Qt6" ]] || die "host Qt not found at $QT_HOST (--setup or QT_HOST)"

  # Fail early and readably when a prebuilt host tool cannot start (missing shared libraries).
  for tool in libexec/moc libexec/rcc libexec/qmlcachegen libexec/qmlimportscanner bin/lrelease; do
    out=$("$QT_HOST/$tool" --help 2>&1 || true)
    if grep -q 'error while loading shared libraries' <<<"$out"; then
      die "Qt host tool $QT_HOST/$tool does not start: $(head -n 1 <<<"$out")"
    fi
  done

  # shellcheck disable=SC1091
  EMSDK_QUIET=1 source "$EMSDK/emsdk_env.sh" >/dev/null
  emcc_version=$(emcc -dumpversion)
  if [[ "$emcc_version" != "$EMSDK_VERSION" ]]; then
    die "emcc $emcc_version found, Qt $QT_VERSION needs $EMSDK_VERSION (emsdk activate $EMSDK_VERSION)"
  fi

  if [[ $do_clean == 1 ]]; then
    log "removing $BUILD_DIR"
    rm -rf "$BUILD_DIR"
  fi

  launcher=()
  if command -v ccache >/dev/null; then
    launcher=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
  fi
  # Qt's toolchain file finds Emscripten through $EMSDK. (cmake + toolchain file rather than
  # bin/qt-cmake: aqt's all_os WebAssembly package is built on Windows, its scripts are not
  # executable and point at C:/.)
  log "configure ($BUILD_TYPE) in $BUILD_DIR"
  cmake -S "$root" -B "$BUILD_DIR" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DQT_HOST_PATH="$QT_HOST" "${launcher[@]}" >&2
  log "build (-j $JOBS)"
  cmake --build "$BUILD_DIR" --target brulion -j "$JOBS" >&2
fi

# ---- assemble the site ----------------------------------------------------------------------
app_dir=$BUILD_DIR/src/app
for f in brulion.js brulion.wasm qtloader.js; do
  [[ -f "$app_dir/$f" ]] || die "$app_dir/$f missing (build first)"
done

log "assembling $DIST_DIR"
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR/icons"
cp "$app_dir/brulion.js" "$app_dir/brulion.wasm" "$app_dir/qtloader.js" "$DIST_DIR/"
cp wasm/manifest.webmanifest "$DIST_DIR/"
cp wasm/icons/*.png "$DIST_DIR/icons/"
cp assets/logo.svg "$DIST_DIR/icons/logo.svg"
# GitHub Pages: serve files as they are (no Jekyll processing).
touch "$DIST_DIR/.nojekyll"

# One version for the three app files: their URLs carry ?v=<hash>, so HTTP caches never mix
# brulion.js and brulion.wasm of different builds.
asset_version=$(cat "$DIST_DIR/brulion.js" "$DIST_DIR/brulion.wasm" "$DIST_DIR/qtloader.js" | sha256)
wasm_size=$(wc -c <"$DIST_DIR/brulion.wasm" | tr -d ' ')
sed -e "s/@BRULION_WASM_SIZE@/$wasm_size/g" -e "s/@BRULION_ASSET_VERSION@/$asset_version/g" \
  wasm/index.html >"$DIST_DIR/index.html"

precache=(index.html manifest.webmanifest
  "qtloader.js?v=$asset_version" "brulion.js?v=$asset_version" "brulion.wasm?v=$asset_version")
for icon in "$DIST_DIR"/icons/*; do
  precache+=("icons/$(basename "$icon")")
done
precache_json=$(printf '"%s",' "${precache[@]}")
precache_json="[${precache_json%,}]"
cache_version=$(cd "$DIST_DIR" && find . -type f ! -name sw.js | LC_ALL=C sort | while read -r f; do
  printf '%s ' "$f"
  sha256 <"$f"
done | sha256)
sed -e "s/@BRULION_CACHE_VERSION@/$cache_version/g" \
  -e "s|\['@BRULION_PRECACHE@'\]|$precache_json|" wasm/sw.js >"$DIST_DIR/sw.js"

grep -qE '@BRULION_[A-Z_]+@' "$DIST_DIR/index.html" "$DIST_DIR/sw.js" && die "unreplaced placeholder in index.html/sw.js"

# ---- report ---------------------------------------------------------------------------------
human() { awk -v b="$1" 'BEGIN { printf "%.2f MiB (%d B)", b / 1048576, b }'; }
wasm_gzip=$(gzip -9 -c "$DIST_DIR/brulion.wasm" | wc -c | tr -d ' ')
js_size=$(wc -c <"$DIST_DIR/brulion.js" | tr -d ' ')
js_gzip=$(gzip -9 -c "$DIST_DIR/brulion.js" | wc -c | tr -d ' ')
log "done: $DIST_DIR (cache version $cache_version)"
echo "brulion.wasm  raw $(human "$wasm_size")  gzip -9 $(human "$wasm_gzip")"
if command -v brotli >/dev/null; then
  wasm_br=$(brotli -c -q 11 "$DIST_DIR/brulion.wasm" | wc -c | tr -d ' ')
  echo "brulion.wasm  brotli -q 11 $(human "$wasm_br")"
fi
echo "brulion.js    raw $(human "$js_size")  gzip -9 $(human "$js_gzip")"
echo "total site    $(du -sk "$DIST_DIR" | cut -f1) KiB"
