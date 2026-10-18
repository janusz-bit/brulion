#!/usr/bin/env bash
# Build a source tree for benchmarks on the x86_64 laptop (Release + symbols, no tests).
#
#   tests/bench/app/laptop-build.sh <name> <tree> [variant]
#     <name>     laptop dir ~/work/brulion-perf-<name>/{src,build-<variant>}
#     <tree>     local source tree (BASELINE: `git archive HEAD | tar -x -C <tree>`; CURRENT: an
#                rsync snapshot of the working tree)
#     [variant]  rel (default: Release -O3 + -g, like the shipped build plus symbols for perf)
#                qmldbg (the same + -DQT_QML_DEBUG: qmlprofiler can attach)
#
# Takes the shared laptop lock (LAPTOP_LOCK) for the build; JOBS (default 12).
set -euo pipefail
name="$1"; tree="$2"; variant="${3:-rel}"
lock="${LAPTOP_LOCK:-/tmp/claude-1000/-home-nixos-workspace-note-taking/e33aea8f-4baa-4b18-a73a-399503007e08/scratchpad/laptop.lock}"
jobs="${JOBS:-12}"
root="work/brulion-perf-${name}"
case "$variant" in
  rel) flags="-g" ;;
  qmldbg) flags="-g -DQT_QML_DEBUG" ;;
  *) echo "laptop-build: unknown variant $variant" >&2; exit 2 ;;
esac
ssh -o BatchMode=yes laptop "mkdir -p ${root}/src ${root}/build-${variant}"
rsync -a --delete --exclude='/build*/' --exclude='/.git/' --exclude='/result*' --exclude='/tests/screenshots/' \
  "${tree%/}/" "laptop:${root}/src/"
remote=$(cat <<EOS
set -euo pipefail
cd \$HOME/${root}/src
export CCACHE_DIR=\$HOME/.cache/ccache CCACHE_BASEDIR=\$HOME/work
if [[ ! -f ../build-${variant}/build.ninja ]]; then
  cmake -S . -B ../build-${variant} -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS='${flags}' \
    -DBUILD_TESTING=OFF -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
fi
cmake --build ../build-${variant} -j ${jobs} --target brulion
ls -l ../build-${variant}/src/app/brulion
EOS
)
flock -s "$lock" ssh -o BatchMode=yes laptop "cd ${root}/src && nix develop path:. -c bash -c $(printf '%q' "$remote")"
