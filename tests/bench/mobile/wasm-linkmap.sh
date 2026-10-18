#!/usr/bin/env bash
# wasm-linkmap.sh <source tree> <build dir> <map file> — the browser app built exactly like
# scripts/wasm-build.sh (MinSizeRel, Qt 6.11.2 wasm_singlethread, emsdk 4.0.7, ccache) in its own
# build dir, with the link also writing a wasm-ld map (-Wl,-Map). The map lists every input chunk
# (function / data segment) with the object or archive member it came from — before wasm-opt,
# so code sizes are ~10–20 % above the shipped ones. Summarise with wasm-linkmap.py.
# x86_64 Linux (the laptop); on NixOS it enters a nix shell like wasm-build.sh does.
set -euo pipefail
script=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")
src=$(cd "$1" && pwd)
build=$2
map=$3
cache=${BRULION_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}}
EMSDK=${EMSDK:-$cache/brulion-emsdk/emsdk}
QT_ROOT=${QT_ROOT:-$cache/brulion-qt}
QT_WASM=${QT_WASM:-$QT_ROOT/6.11.2/wasm_singlethread}
QT_HOST=${QT_HOST:-$QT_ROOT/6.11.2/gcc_64}
if [[ -e /etc/NIXOS && -z ${BRULION_WASM_IN_NIX_SHELL:-} ]]; then
  libs=$(nix build --no-link --print-out-paths nixpkgs#glib.out nixpkgs#krb5.lib nixpkgs#brotli.lib nixpkgs#keyutils.lib)
  ld_path=
  for p in $libs; do ld_path+="$p/lib:"; done
  export NIX_LD_LIBRARY_PATH="${ld_path}${NIX_LD_LIBRARY_PATH:-/run/current-system/sw/share/nix-ld/lib}"
  export BRULION_WASM_IN_NIX_SHELL=1
  exec nix shell nixpkgs#cmake nixpkgs#ninja nixpkgs#ccache nixpkgs#python3 --command bash "$script" "$@"
fi
EMSDK_QUIET=1 source "$EMSDK/emsdk_env.sh" >/dev/null
cmake -S "$src" -B "$build" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$QT_WASM/lib/cmake/Qt6/qt.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=MinSizeRel -DQT_HOST_PATH="$QT_HOST" \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  "-DCMAKE_EXE_LINKER_FLAGS=-Wl,-Map=$map" >/dev/null
cmake --build "$build" --target brulion -j "${JOBS:-8}" | tail -3
ls -la "$map" "$build/src/app/brulion.wasm"
