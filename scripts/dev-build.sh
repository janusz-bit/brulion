#!/usr/bin/env bash
# Configure (if needed), build and optionally test — serialised machine-wide with a lock, so
# parallel agents on the low-memory dev machine never run two compilations at once.
#
#   scripts/dev-build.sh                       # build everything in ./build
#   scripts/dev-build.sh -t tst_dates          # build + run tests matching the regex
#   BUILD_DIR=build-x scripts/dev-build.sh     # other build directory
#   JOBS=4 scripts/dev-build.sh                # more parallel jobs (only on bigger machines)
#   BRULION_CLANG=1 BUILD_DIR=build-clang scripts/dev-build.sh   # Clang even outside the dev shell
#
# Must be run inside `nix develop` (provides Qt, cmake, ninja, ccache and Clang: its `cc` / `c++`
# are nixpkgs' Clang over GCC's libstdc++, the compiler of the Nix package and of every other
# build — docs/PERFORMANCE.md "GCC or Clang").
#
# BRULION_CLANG=1 configures a new build directory with the flake's own Clang (nixpkgs#clang of
# flake.lock) whatever `c++` is. The compiler of an existing build directory never changes (one
# configured with GCC before 2026-11-19 stays GCC): use a new BUILD_DIR.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

# A fixed path, not $TMPDIR: `nix develop` gives every shell its own TMPDIR
# (/tmp/nix-shell.XXXXXX), which would make the lock per shell instead of machine-wide.
if [[ -z "${BRULION_BUILD_LOCKED:-}" ]] && command -v flock >/dev/null 2>&1; then
  export BRULION_BUILD_LOCKED=1
  exec flock "${BRULION_BUILD_LOCK:-/tmp/brulion-build.lock}" "$0" "$@"
fi

tests=""
targets=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    -t) tests="$2"; shift 2 ;;
    *) targets+=("$1"); shift ;;
  esac
done

build_dir="${BUILD_DIR:-build}"
jobs="${JOBS:-2}"

if [[ ! -f "${build_dir}/build.ninja" ]]; then
  if [[ -n "${BRULION_CLANG:-}" ]]; then
    clang_dir=$(nix build --no-link --print-out-paths --inputs-from . nixpkgs#clang)
    export CC="${clang_dir}/bin/clang" CXX="${clang_dir}/bin/clang++"
  fi
  cmake -S . -B "${build_dir}" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DBRULION_WERROR=ON
elif [[ -n "${BRULION_CLANG:-}" ]] && ! grep -q '^CMAKE_CXX_COMPILER:.*clang++' "${build_dir}/CMakeCache.txt"; then
  echo "dev-build: ${build_dir} was configured with another compiler; BRULION_CLANG=1 needs a new BUILD_DIR" >&2
  exit 2
fi

if [[ ${#targets[@]} -gt 0 ]]; then
  cmake --build "${build_dir}" -j "${jobs}" --target "${targets[@]}"
else
  cmake --build "${build_dir}" -j "${jobs}"
fi

if [[ -n "${tests}" ]]; then
  QT_QPA_PLATFORM=offscreen ctest --test-dir "${build_dir}" --output-on-failure -R "${tests}"
fi
