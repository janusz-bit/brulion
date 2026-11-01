#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Tests the time budget of scripts/bench.sh (-T SECONDS: no repetition starts once the run has
# taken that long; what is left is listed, exit status 1) with stand-in suites: shell scripts that
# write one QTest CSV row and sleep, in a throwaway build dir (no build, no machine lock).
#
# Usage: tests/scripts/tst_bench_budget.sh [path/to/bench.sh]   (default: scripts/bench.sh)
set -euo pipefail

script=$(realpath -- "${1:-$(dirname -- "${BASH_SOURCE[0]}")/../../scripts/bench.sh}")
[[ -f $script ]] || {
  echo "no $script" >&2
  exit 2
}
command -v python3 >/dev/null 2>&1 || {
  echo "SKIP   : python3 (bench.sh's summary) not found"
  exit 77
}
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT

failed=0
pass() { printf 'PASS   : %s\n' "$1"; }
fail() {
  printf 'FAIL!  : %s\n' "$1"
  failed=$((failed + 1))
}
expect() { # <description> <command...>: the command succeeds
  local what=$1
  shift
  if "$@"; then pass "$what"; else fail "$what"; fi
}

# Stand-in suites: each run sleeps $FAKE_SLEEP s and writes a row "<function>","ąćęłńóśźż <n>".
mkdir -p "$tmp/build/tests/bench"
for suite in bench_text bench_index; do
  # This bash, not /usr/bin/env (the Nix build sandbox has none).
  printf '#!%s\n' "$BASH" >"$tmp/build/tests/bench/$suite"
  cat >>"$tmp/build/tests/bench/$suite" <<'SH'
csv=""
while (($#)); do
  case $1 in
    -o) [[ $2 == *,csv ]] && csv=${2%,csv}; [[ $2 == *,txt ]] && : >"${2%,txt}"; shift 2 ;;
    *) shift ;;
  esac
done
sleep "${FAKE_SLEEP:-0}"
printf '"%s","%s","WalltimeMilliseconds",1.5,1.5,1\n' "${0##*/}Fn" "ąćęłńóśźż 1000" >"$csv"
SH
  chmod +x "$tmp/build/tests/bench/$suite"
done
# Not built from this tree's benchmark sources (no hash inside): measured anyway.
export BRULION_BENCH_ALLOW_STALE=1 BRULION_BUILD_LOCKED=1

# run <name> <bench.sh options...>: sets $status, $out (stdout + stderr) and $dir (results).
run() {
  dir="$tmp/$1"
  shift
  status=0
  out=$(bash "$script" run -n -b "$tmp/build" -o "$dir" "$@" 2>&1) || status=$?
  printf '%s\n' "$out" | sed 's/^/        | /'
}
has() { grep -qF -- "$1" <<<"$out"; }
runs() { find "$dir/raw" -name '*.csv' | wc -l; }
summary_n() { # <suite>: the number of repetitions in the summary's row of that suite
  awk -F, -v s="$1" '$1 == s { print $5 }' "$dir/summary.csv"
}

# 1. No budget: every repetition runs.
FAKE_SLEEP=0 run none -r 2 bench_text bench_index
expect "no budget: exit 0" test "$status" -eq 0
expect "no budget: 2 suites x 2 repetitions" test "$(runs)" -eq 4
expect "no budget: nothing reported as skipped" eval '! has "used up"'
expect "no budget: env.txt says so" grep -qx '.*time budget: none' "$dir/env.txt"

# 2. A budget the run stays within.
FAKE_SLEEP=0 run plenty -r 2 -T 600 bench_text bench_index
expect "budget not reached: exit 0" test "$status" -eq 0
expect "budget not reached: every repetition" test "$(runs)" -eq 4
expect "budget not reached: env.txt records it" grep -qx '.*time budget: 600 s' "$dir/env.txt"

# 3. Used up during the first suite: its repetition in progress finishes, the rest is skipped.
# (Measured to the millisecond: a second ticking over right after the start does not count.)
FAKE_SLEEP=1.2 run cut -r 3 -T 1 bench_text "bench_index:bench_indexFn"
expect "budget used up: exit 1" test "$status" -eq 1
expect "budget used up: only the first repetition ran" test "$(runs)" -eq 1
expect "budget used up: the rest of the first suite is listed" has "bench_text (repetitions 2-3)"
expect "budget used up: the second selection is listed whole" has "  bench_index:bench_indexFn"
expect "budget used up: the message is kept in env.txt" grep -q 'time budget of 1 s used up' "$dir/env.txt"
expect "budget used up: the summary has what was measured" test "$(summary_n bench_text)" = 1
expect "budget used up: the summary keeps the Polish tag" grep -q 'ąćęłńóśźż 1000' "$dir/summary.csv"

# 3b. A second ticking over right after the start (here: a `date` whose whole seconds jump by one
# after its first call) is no second of the budget used up: the first repetition runs.
mkdir -p "$tmp/tick"
cat >"$tmp/tick/date" <<SH
#!$BASH
if [[ \$* == +%s ]]; then
  if [[ -e "$tmp/tick/called" ]]; then echo 1001; else : >"$tmp/tick/called"; echo 1000; fi
else
  exec "$(command -v date)" "\$@"
fi
SH
chmod +x "$tmp/tick/date"
PATH="$tmp/tick:$PATH" FAKE_SLEEP=0 run tick -r 1 -T 1 bench_text
expect "a second ticking over: exit 0" test "$status" -eq 0
expect "a second ticking over: the repetition ran" test "$(runs)" -eq 1
expect "a second ticking over: nothing reported as skipped" eval '! has "used up"'

# 4. A budget of 0: nothing starts.
FAKE_SLEEP=0 run zero -r 2 -T 0 bench_text
expect "budget 0: exit 1" test "$status" -eq 1
expect "budget 0: nothing ran" test "$(runs)" -eq 0
expect "budget 0: the suite is listed" has "  bench_text"

# 5. Malformed budgets are usage errors (exit 2), nothing runs.
i=0
# Leading zeros: bash arithmetic would read 010 as octal (8 s) and 08 not at all (no budget).
for bad in abc -5 1.5 '' ' 3' '١٢' 'ąę' 08 09 010 00 0600; do
  i=$((i + 1))
  FAKE_SLEEP=0 run "bad$i" -r 1 -T "$bad" bench_text
  expect "budget '$bad': usage error" test "$status" -eq 2
  expect "budget '$bad': says what is wrong" has "-T takes whole seconds"
done

if ((failed)); then
  echo "Totals: $failed failed"
  exit 1
fi
echo "Totals: all passed"
