#!/usr/bin/env bash
# Core benchmarks (tests/bench, QtTest QBENCHMARK, one executable per core module): configure a
# Release build dir with -DBRULION_BUILD_BENCHMARKS=ON, build the suites, run each one N times
# (a fresh process per repetition) writing QTest CSV, summarise (median, min, max and spread per
# row), compare two runs (% change against the noise) and check the scaling between input sizes.
#
#   scripts/bench.sh [run] [options] [suite[:function[:tag]]…]
#       -b DIR      build dir (default build-bench)
#       -r N        repetitions per suite (default 5)
#       -o DIR      output dir (default <build dir>/bench-results/<date>-<host>); repeated runs
#                   into one dir add to it (raw files of the same suite are overwritten)
#       -p PROFILE  full (default) or small (Raspberry Pi / phone class: drops the largest sizes)
#       -t TYPE     CMAKE_BUILD_TYPE of a new build dir (default Release; RelWithDebInfo to profile)
#       -c CPUS     pin the runs to these CPUs (taskset list, e.g. 0-11: the P-cores of a hybrid
#                   Intel CPU, so the scheduler never moves a run to a slower E-core)
#       -q LOAD     before every repetition wait (at most 10 min) until the 1-minute load average
#                   is below LOAD (another build or benchmark on the machine distorts timings)
#       -n          no configure/build (run what is built)
#       -B          configure/build only (no run)
#       suites: bench_text bench_dates bench_calc bench_markdown bench_index bench_vault bench_sync
#               bench_reminders bench_io (default: all); "bench_index:search" runs one function,
#               "bench_index:search:10000 notes fuzzy" one data row.
#   scripts/bench.sh summarize DIR      -> DIR/summary.csv (+ a table on stdout)
#   scripts/bench.sh compare BASE NEW [--pct P] [--all] [--csv OUT] [--filter REGEX]
#                                       rows whose median changed by more than max(P %, noise)
#                                       (default P = 10; BASE/NEW: summary.csv files or result dirs)
#   scripts/bench.sh scaling DIR|SUMMARY [--all] [--csv OUT]
#                                       exponent k of time ~ n^k between the sizes of a series
#                                       ("flat list 1000 items" / "5000 items" …): k ≈ 2 is quadratic
#   scripts/bench.sh list [suite…]      the functions of the built suites (-b DIR first if needed)
#
# Typical use
#   scripts/bench.sh                                  # build + run everything (Pi defaults: -j2)
#   JOBS=16 scripts/bench.sh -c 0-11 -q 2.5 -o ../r/new bench_index bench_markdown   # laptop
#   scripts/bench.sh -n -r 3 bench_index:search       # one function, existing build
#   scripts/bench.sh compare ../r/base ../r/new --csv ../r/compare.csv
#   scripts/bench.sh scaling ../r/new
# To compare two source trees (e.g. a release and a patched tree): give both the same bench sources
# (tests/bench, tests/CMakeLists.txt, this script), build each in its own dir, run the suites
# alternately under the same conditions (same -c/-q), then `compare` the two result dirs.
#
# Every suite carries the hash of the tests/bench sources it was built from
# (tests/bench/sourcehash.cmake); a run refuses a binary whose hash differs from the source tree
# (a stale build, e.g. after copying a tree whose files kept older mtimes) and records the hash in
# env.txt. The inputs' digests ("[bench] input …: N bytes, sha256 …") must match across machines.
#
# Environment: JOBS (build jobs, default 2 — the Pi; use JOBS=16 on the laptop), BRULION_BENCH_TMP
# (where vaults are written; default <build dir>/bench-tmp, i.e. a real disk, not a tmpfs /tmp),
# BRULION_BENCH_ALLOW_STALE=1 (measure binaries whose source hash differs, with a warning).
# Must run inside `nix develop` (Qt, cmake, ninja). Holds the machine-wide build lock (like
# scripts/dev-build.sh) for the whole run, so benchmarks never compete with a compilation.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

report_py="tests/bench/benchcsv.py"
all_suites=(bench_text bench_dates bench_calc bench_markdown bench_index bench_vault bench_sync bench_reminders bench_io)

# The header comment above is the help text.
usage() { awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; }

# SHA-256 of the manifest "<sha256>  <file>" of tests/bench/{*.cpp,*.h,*.cmake,CMakeLists.txt} in
# byte order: the same value tests/bench/sourcehash.cmake compiles into every suite.
source_hash() {
  local files
  mapfile -t files < <(cd tests/bench && printf '%s\n' ./*.cpp ./*.h ./*.cmake ./CMakeLists.txt | sed 's|^\./||' | LC_ALL=C sort)
  (cd tests/bench && sha256sum -- "${files[@]}") | sha256sum | cut -d' ' -f1
}
binary_hash() { grep -aoE 'brulion-bench-sources:[0-9a-f]{64}' "$1" 2>/dev/null | head -1 | cut -d: -f2 || true; }

case "${1:-}" in
  summarize) shift; exec python3 "${report_py}" summarize "$@" ;;
  compare) shift; exec python3 "${report_py}" compare "$@" ;;
  scaling) shift; exec python3 "${report_py}" scaling "$@" ;;
  -h|--help|help) usage; exit 0 ;;
  list)
    shift
    dir="build-bench"
    if [[ "${1:-}" == -b ]]; then dir="$2"; shift 2; fi
    [[ $# -gt 0 ]] || set -- "${all_suites[@]}"
    for s in "$@"; do
      echo "${s}:"
      "${dir}/tests/bench/${s}" -functions 2>/dev/null | sed 's/^/  /' || echo "  (not built in ${dir})"
    done
    exit 0 ;;
  run) shift ;;
esac

if [[ -z "${BRULION_BUILD_LOCKED:-}" ]] && command -v flock >/dev/null 2>&1; then
  export BRULION_BUILD_LOCKED=1
  exec flock "${BRULION_BUILD_LOCK:-/tmp/brulion-build.lock}" "$0" "$@"
fi

build_dir="build-bench"; reps=5; out=""; profile="full"; build_type="${BUILD_TYPE:-Release}"; build=1; run=1
cpus=""; quiet=""
while getopts "b:r:o:p:t:c:q:nBh" opt; do
  case "${opt}" in
    b) build_dir="${OPTARG}" ;;
    r) reps="${OPTARG}" ;;
    o) out="${OPTARG}" ;;
    p) profile="${OPTARG}" ;;
    t) build_type="${OPTARG}" ;;
    c) cpus="${OPTARG}" ;;
    q) quiet="${OPTARG}" ;;
    n) build=0 ;;
    B) run=0 ;;
    h) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
  esac
done
shift $((OPTIND - 1))
selected=("$@")
[[ ${#selected[@]} -gt 0 ]] || selected=("${all_suites[@]}")

if [[ ${build} == 1 ]]; then
  if [[ ! -f "${build_dir}/build.ninja" ]] || ! grep -q '^BRULION_BUILD_BENCHMARKS:BOOL=ON' "${build_dir}/CMakeCache.txt"; then
    launcher=()
    command -v ccache >/dev/null 2>&1 && launcher=(-DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
    cmake -S . -B "${build_dir}" -G Ninja -DCMAKE_BUILD_TYPE="${build_type}" -DBRULION_BUILD_BENCHMARKS=ON \
      "${launcher[@]}"
  fi
  # Compilation goes through dev-build.sh (the lock is already held).
  BUILD_DIR="${build_dir}" JOBS="${JOBS:-2}" scripts/dev-build.sh brulion_benchmarks
fi
[[ ${run} == 1 ]] || exit 0

# Refuse binaries built from other benchmark sources than this tree's (see the header).
tree_hash=$(source_hash)
stale=0
for sel in "${selected[@]}"; do
  exe="${build_dir}/tests/bench/${sel%%:*}"
  [[ -x "${exe}" ]] || continue
  bin_hash=$(binary_hash "${exe}")
  if [[ "${bin_hash}" != "${tree_hash}" ]]; then
    echo "bench: ${exe} was built from other benchmark sources (binary ${bin_hash:-without a hash}, tree ${tree_hash}); rebuild it (run without -n)" >&2
    stale=1
  fi
done
if [[ ${stale} == 1 && -z "${BRULION_BENCH_ALLOW_STALE:-}" ]]; then
  exit 2
fi

pin=()
if [[ -n "${cpus}" ]]; then
  command -v taskset >/dev/null 2>&1 || { echo "bench: -c needs taskset (util-linux)" >&2; exit 2; }
  pin=(taskset -c "${cpus}")
fi

# Wait until the machine is quiet (1-minute load average below $quiet), at most 10 minutes.
wait_quiet() {
  [[ -n "${quiet}" ]] || return 0
  local waited=0
  while ! awk -v l="$(cut -d' ' -f1 /proc/loadavg)" -v q="${quiet}" 'BEGIN { exit !(l < q) }'; do
    if ((waited >= 600)); then
      echo "bench: load still $(cut -d' ' -f1 /proc/loadavg) after 10 min, running anyway" >&2
      return 0
    fi
    sleep 10
    waited=$((waited + 10))
  done
}

host=$(hostname -s 2>/dev/null || hostname)
[[ -n "${out}" ]] || out="${build_dir}/bench-results/$(date +%Y%m%d-%H%M%S)-${host}"
mkdir -p "${out}/raw"
export BRULION_BENCH_TMP="${BRULION_BENCH_TMP:-$(cd "${build_dir}" && pwd)/bench-tmp}"
export BRULION_BENCH_PROFILE="${profile}"
export QT_QPA_PLATFORM=offscreen
# QCoreApplication adopts the environment's locale: keep '.' as the decimal point in the CSV.
unset LC_ALL
export LC_NUMERIC=C

{
  echo "--- ${*:-all suites}"
  echo "date: $(date -Iseconds)"
  echo "host: ${host} ($(uname -m), $(nproc) CPUs)"
  echo "cpu: $(grep -m1 -E 'model name|Model' /proc/cpuinfo | cut -d: -f2- | sed 's/^ *//')"
  echo "governor: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo unknown)"
  echo "pinned: ${cpus:-no}; quiet below: ${quiet:-not waited}"
  echo "load: $(cut -d' ' -f1-3 /proc/loadavg)"
  echo "profile: ${profile}; repetitions: ${reps}; build: $(grep -m1 '^CMAKE_BUILD_TYPE' "${build_dir}/CMakeCache.txt" | cut -d= -f2) (${build_dir})"
  echo "git: $(git rev-parse --short HEAD 2>/dev/null || echo none) $(git status --porcelain 2>/dev/null | wc -l | tr -d ' ') changed files"
  echo "bench sources: ${tree_hash}$([[ ${stale} == 1 ]] && echo ' (STALE binaries measured: BRULION_BENCH_ALLOW_STALE)')"
  echo "tmp: ${BRULION_BENCH_TMP} ($(findmnt -n -o FSTYPE --target "$(dirname "${BRULION_BENCH_TMP}")" 2>/dev/null || echo ?))"
} | tee -a "${out}/env.txt"

status=0
for sel in "${selected[@]}"; do
  suite="${sel%%:*}"
  args=()
  [[ "${sel}" == *:* ]] && args+=("${sel#*:}")
  exe="${build_dir}/tests/bench/${suite}"
  [[ -x "${exe}" ]] || { echo "bench: ${exe} not built" >&2; status=1; continue; }
  tagname="${suite}"
  [[ "${sel}" == *:* ]] && tagname+="@$(printf '%s' "${sel#*:}" | tr -c 'A-Za-z0-9_-' '_')"
  for ((k = 1; k <= reps; k++)); do
    wait_quiet
    echo "== ${sel} (${k}/${reps}) load $(cut -d' ' -f1 /proc/loadavg)" | tee -a "${out}/env.txt"
    mkdir -p "${BRULION_BENCH_TMP}"
    start=$(date +%s)
    if ! "${pin[@]}" "${exe}" -o "${out}/raw/${tagname}.${k}.csv,csv" -o "${out}/raw/${tagname}.${k}.txt,txt" "${args[@]}" \
        > /dev/null; then
      echo "bench: ${sel} failed (see ${out}/raw/${tagname}.${k}.txt)" >&2
      status=1
    fi
    echo "   $(( $(date +%s) - start )) s, load after $(cut -d' ' -f1 /proc/loadavg)" >> "${out}/env.txt"
    grep -E '^(FAIL|QWARN|QFATAL)|\[bench\]' "${out}/raw/${tagname}.${k}.txt" | head -20 || true
  done
done
rm -rf "${BRULION_BENCH_TMP}"

python3 "${report_py}" summarize "${out}" > /dev/null || status=1
echo "bench: results in ${out} (summary.csv)"
exit ${status}
