#!/usr/bin/env bash
# Performance regression check: runs a fixed subset of the core benchmarks (tests/bench) with
# scripts/bench.sh and compares it with the committed baseline of this machine and with the
# machine-independent limits (docs/PERFORMANCE.md, "Regression guards").
#
#   scripts/perf-check.sh [options] [-- bench.sh options…]
#     -m MACHINE  baseline tests/bench/baselines/MACHINE.csv (default: $BRULION_PERF_MACHINE, else
#                 "pi" on a Raspberry Pi 4, "laptop" on the reference laptop (i5-13450HX), else
#                 the host name; a machine without a baseline gets only the limits)
#     -b DIR      bench build dir (default build-bench; configured and built like scripts/bench.sh)
#     -o DIR      results dir (default <build dir>/perf-check/<date>-<machine>)
#     -n          no build (run what is built)
#     -s          scaling only: just the series and memory limits of tests/bench/baselines/limits.csv,
#                 no timing baseline. Machine-independent, so it can run on shared CI runners
#     -u          update: write the baseline of MACHINE from this run (ratchet it down after an
#                 optimisation; never loosen it without a note in the commit message). Record it
#                 from a perf-check run (here or -R): it measures every guard row in a process of
#                 its own, and a row measured after other functions of a whole-suite bench.sh run
#                 can differ by 15 % (bench_index suggestPages 'stro': 2.84 vs 3.31 ms)
#     -R DIR      only check an existing results dir (no build, no run)
#     -h          this help
#   bench.sh options after `--` replace the baseline's own (its "# bench.sh:" line, e.g.
#   "-c 0-11 -q 3 -r 5"; default "-r 5", -s: "-r 3"); -p (profile) follows the machine (pi: small).
#
# Exit 1 when a median exceeds its baseline by more than max(10 %, 3 × spread) (spread =
# (max − min) / median, the larger of the baseline's and this run's), when a series' exponent k
# (time ~ n^k between its two largest sizes) passes its limit, when a value passes its absolute
# limit (index memory), or when a checked row is missing. Exit 2 on usage or build errors.
#
# Typical use
#   JOBS=16 scripts/perf-check.sh                        # laptop: build, run (~5 min), check
#   scripts/perf-check.sh -n -s                          # the CI smoke: scaling + memory only
#   scripts/perf-check.sh -n -u                          # new baseline after an optimisation
#   scripts/perf-check.sh -R build-bench/perf-check/<dir>
# Must run inside `nix develop` (or with Qt, cmake and ninja on PATH), like scripts/bench.sh.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

usage() { awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; }

baselines="tests/bench/baselines"
limits="${baselines}/limits.csv"

# Per-machine guards: one suite:function:tag per row, per profile (the Pi's small profile has
# no 10000-file sync row). Keep in sync with docs/PERFORMANCE.md.
guards_full=(
  "bench_index:openFromDisk:10000 notes scan+read+index"
  "bench_index:firstSearch:10000 notes"
  "bench_index:query:10000 notes tag praca"
  "bench_index:suggestBlocks:10000 notes 'kawa ogród'"
  "bench_index:suggestPages:10000 notes 'stro'"
  "bench_index:memory:10000 notes + search index"
  "bench_reminders:afterEdit:mixed vault 10000 notes, update + all consumers"
  "bench_markdown:blockDocOps:5000 blocks keystroke"
  "bench_markdown:blockDocOps:5000 blocks undo+redo"
  "bench_vault:localStoreFlush:10000 synced files"
  "bench_sync:syncNoChanges:10000 files"
  "bench_index:openFromDisk:10000 notes loader 4 workers"
  "bench_index:openFromDisk:10000 notes loader warm cache 4 workers"
)
guards_small=(
  "${guards_full[@]:0:10}"
  "bench_sync:syncNoChanges:5000 files"
  "bench_index:openFromDisk:10000 notes loader 3 workers"
  "bench_index:openFromDisk:10000 notes loader warm cache 3 workers"
)

detect_machine() {
  if [[ -n "${BRULION_PERF_MACHINE:-}" ]]; then
    echo "${BRULION_PERF_MACHINE}"
  elif grep -qs 'Raspberry Pi 4' /proc/device-tree/model; then
    echo pi
  elif grep -qs 'i5-13450HX' /proc/cpuinfo; then
    echo laptop
  else
    hostname -s 2>/dev/null || hostname
  fi
}

machine=""; build_dir="build-bench"; out=""; build=1; scaling_only=0; update=0; recheck=""
while getopts "m:b:o:nsuR:h" opt; do
  case "${opt}" in
    m) machine="${OPTARG}" ;;
    b) build_dir="${OPTARG}" ;;
    o) out="${OPTARG}" ;;
    n) build=0 ;;
    s) scaling_only=1 ;;
    u) update=1 ;;
    R) recheck="${OPTARG}" ;;
    h) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
  esac
done
shift $((OPTIND - 1))
[[ "${1:-}" != -- ]] || shift
extra=("$@")
[[ -n "${machine}" ]] || machine=$(detect_machine)
[[ "${machine}" =~ ^[A-Za-z0-9_.-]+$ ]] || { echo "perf-check: bad machine name '${machine}'" >&2; exit 2; }
baseline="${baselines}/${machine}.csv"
if [[ ${scaling_only} == 1 && ${update} == 1 ]]; then
  echo "perf-check: -u needs the full check (not -s)" >&2
  exit 2
fi

profile=full
[[ "${machine}" != pi ]] || profile=small
if [[ -f "${baseline}" ]]; then
  p=$(sed -n 's/^# profile: *//p' "${baseline}" | head -1)
  [[ -z "${p}" ]] || profile="${p}"
fi
if [[ ${profile} == small ]]; then guards=("${guards_small[@]}"); else guards=("${guards_full[@]}"); fi

# bench.sh options: from the command line, else from the baseline (timing needs the conditions
# it was recorded under), else a default.
opts=()
if [[ ${#extra[@]} -gt 0 ]]; then
  opts=("${extra[@]}")
elif [[ ${scaling_only} == 0 && -f "${baseline}" ]]; then
  read -r -a opts <<< "$(sed -n 's/^# bench.sh: *//p' "${baseline}" | head -1)"
fi
[[ ${#opts[@]} -gt 0 ]] || { if [[ ${scaling_only} == 1 ]]; then opts=(-r 3); else opts=(-r 5); fi; }

# What to run: whole functions for the series of the limits file (every size of the profile),
# single rows for the per-machine guards (unless their function already runs whole).
selectors=()
declare -A whole=()
while IFS=, read -r kind suite function row _; do
  [[ "${kind}" == scaling || "${kind}" == max ]] || continue
  if [[ "${kind}" == scaling ]]; then
    sel="${suite}:${function}"
  else
    sel="${suite}:${function}:${row}"
  fi
  if [[ -z "${whole[${sel}]:-}" ]]; then
    whole[${sel}]=1
    selectors+=("${sel}")
  fi
done < <(grep -v '^#' "${limits}" | tail -n +2)
if [[ ${scaling_only} == 0 ]]; then
  for g in "${guards[@]}"; do
    fn="${g%:*}"
    [[ -n "${whole[${fn}]:-}" || -n "${whole[${g}]:-}" ]] || selectors+=("${g}")
  done
fi

if [[ -n "${recheck}" ]]; then
  out="${recheck}"
else
  [[ -n "${out}" ]] || out="${build_dir}/perf-check/$(date +%Y%m%d-%H%M%S)-${machine}"
  args=(-b "${build_dir}" -o "${out}" -p "${profile}" "${opts[@]}")
  [[ ${build} == 1 ]] || args+=(-n)
  echo "perf-check: machine ${machine}, profile ${profile}, bench.sh ${opts[*]}, ${#selectors[@]} selections"
  status=0
  scripts/bench.sh run "${args[@]}" "${selectors[@]}" || status=$?
  if [[ ${status} == 2 ]]; then exit 2; fi
  [[ ${status} == 0 ]] || echo "perf-check: some benchmark runs failed (status ${status}); checking what was measured" >&2
fi
summary="${out}/summary.csv"
[[ -f "${summary}" ]] || { echo "perf-check: no ${summary}" >&2; exit 2; }

if [[ ${update} == 1 ]]; then
  mkdir -p "${baselines}"
  python3 - "${summary}" "${baseline}" "${profile}" "${opts[*]}" "${out}" "${guards[@]}" <<'PY'
import csv, sys, datetime, os, re
summary, path, profile, opts, out, *guards = sys.argv[1:]
rows = {(r["suite"], r["function"], r["tag"]): r for r in csv.DictReader(open(summary, encoding="utf-8"))}
env = open(os.path.join(out, "env.txt"), encoding="utf-8").read() if os.path.exists(os.path.join(out, "env.txt")) else ""
def field(name):
    m = re.search(rf"^{name}: (.*)$", env, re.M)
    return m.group(1) if m else "?"
with open(path, "w", newline="", encoding="utf-8") as f:
    f.write(f"# Baseline of scripts/perf-check.sh for this machine: medians and spreads of the guard rows.\n")
    f.write(f"# Recorded {datetime.date.today()} (run {os.path.basename(os.path.normpath(out))})\n# cpu: {field('cpu')}\n")
    f.write(f"# bench sources: {field('bench sources')}\n# profile: {profile}\n# bench.sh: {opts}\n")
    w = csv.writer(f, lineterminator="\n")
    w.writerow(["suite", "function", "tag", "unit", "median", "spread_pct"])
    missing = []
    for g in guards:
        suite, function, tag = g.split(":", 2)
        r = rows.get((suite, function, tag))
        if not r:
            missing.append(g)
            continue
        w.writerow([suite, function, tag, r["unit"], r["median"], r["spread_pct"]])
if missing:
    sys.exit("perf-check: not in this run: " + "; ".join(missing))
print(f"perf-check: wrote {path}")
PY
fi

check=(python3 tests/bench/benchcsv.py check "${summary}" --limits "${limits}" --profile "${profile}"
  --csv "${out}/perf-check.csv")
if [[ ${scaling_only} == 0 ]]; then
  if [[ -f "${baseline}" ]]; then
    check+=(--baseline "${baseline}")
  else
    echo "perf-check: no baseline ${baseline} for this machine: only the limits are checked (record one with -u)" >&2
  fi
fi
"${check[@]}"
