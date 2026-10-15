#!/usr/bin/env bash
# Build and test on the x86_64 laptop (16 cores, 31 GB) instead of this small Pi.
#
# The working tree (including untracked files, excluding build outputs and .git) is rsynced to
# laptop:~/work/brulion-<name>/src with mtimes preserved, so rebuilds are incremental; the build
# dir lives next to it (~/work/brulion-<name>/build). Each agent/user picks its own <name> so
# several builds can run at the same time without interfering.
#
#   scripts/remote-build.sh -n core                       # build everything
#   scripts/remote-build.sh -n core -t '^tst_dates' tst_dates   # build target(s), run ctest regex
#   scripts/remote-build.sh -n ui --run 'scripts/screenshots.sh'  # run a command in the dev shell
#   scripts/remote-build.sh -n ui --pull tests/screenshots   # copy a directory back afterwards
#   scripts/remote-build.sh -n core -k -t '^tst_core'   # -k: keep going past failing targets
#   scripts/remote-build.sh -n core -x src/app/shell -x tests/app/shell   # leave out another
#       agent's unfinished directories (they are not copied, so CMake never sees them)
#
# Environment: REMOTE_HOST (default: laptop), JOBS (default 16), BUILD_TYPE (default Debug).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

name="default"; tests=""; run=""; pulls=(); targets=(); excludes=(); keep_going=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    -n) name="$2"; shift 2 ;;
    -t) tests="$2"; shift 2 ;;
    --run) run="$2"; shift 2 ;;
    --pull) pulls+=("$2"); shift 2 ;;
    -x) excludes+=("--exclude=/${2#/}"); shift 2 ;;
    -k) keep_going=1; shift ;;
    *) targets+=("$1"); shift ;;
  esac
done
[[ "${name}" =~ ^[A-Za-z0-9_-]+$ ]] || { echo "remote-build: bad name '${name}'" >&2; exit 2; }

host="${REMOTE_HOST:-laptop}"
jobs="${JOBS:-16}"
build_type="${BUILD_TYPE:-Debug}"
remote_root="work/brulion-${name}"

ssh -o BatchMode=yes "${host}" "mkdir -p ${remote_root}/src ${remote_root}/build"
rsync -a --delete \
  --exclude='/build*/' --exclude='/.git/' --exclude='/result*' --exclude='/.direnv/' \
  --exclude='/tests/screenshots/' --exclude='/dist-*/' --exclude='*.o' \
  ${excludes[@]+"${excludes[@]}"} \
  ./ "${host}:${remote_root}/src/"

target_args=""
if [[ ${#targets[@]} -gt 0 ]]; then
  target_args="--target $(printf '%q ' "${targets[@]}")"
fi

remote_script=$(cat <<EOS
set -euo pipefail
cd \$HOME/${remote_root}/src
export CCACHE_DIR=\$HOME/.cache/ccache
if [[ ! -f ../build/build.ninja ]]; then
  cmake -S . -B ../build -G Ninja -DCMAKE_BUILD_TYPE=${build_type} \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DBRULION_WERROR=ON
fi
if [[ ${keep_going} == 1 ]]; then
  # -k: build as much as possible (other agents' unfinished targets may fail) and go on.
  cmake --build ../build -j ${jobs} ${target_args} -- -k 0 || echo "remote-build: some targets failed (-k)" >&2
else
  cmake --build ../build -j ${jobs} ${target_args}
fi
if [[ -n $(printf '%q' "${tests}") ]]; then
  QT_QPA_PLATFORM=offscreen ctest --test-dir ../build --output-on-failure -j 8 -R $(printf '%q' "${tests}")
fi
if [[ -n $(printf '%q' "${run}") ]]; then
  export BRULION_BUILD_DIR=\$(cd ../build && pwd)
  bash -c $(printf '%q' "${run}")
fi
EOS
)

# 'path:.' makes the flake include untracked files (there is no .git on the remote copy anyway).
# `|| status=$?`: with `set -e` a failed build would otherwise end this script here, before
# the --pull copies (e.g. screenshots of a failing run) and without the build's status.
status=0
ssh -o BatchMode=yes "${host}" "cd ${remote_root}/src && nix develop path:. -c bash -c $(printf '%q' "${remote_script}")" ||
  status=$?

for p in "${pulls[@]}"; do
  mkdir -p "${p}"
  rsync -a "${host}:${remote_root}/src/${p}/" "${p}/" || echo "remote-build: could not copy ${p} back" >&2
done
exit ${status}
