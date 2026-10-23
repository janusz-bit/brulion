#!/usr/bin/env bash
# The whole app benchmark matrix, BASELINE vs CURRENT (or any labelled builds), one scenario
# group at a time; each group can hold an exclusive lock while it measures (LOCK).
#
#   BINS="base=/path/base/brulion cur=/path/cur/brulion" PROBE=libbenchprobe.so \
#   VAULT_DEMO=vaults/demo VAULT_BIG=vaults/big10k OUT=results/laptop PLATFORM=xvfb RUNS=5 \
#   LOCK=/path/laptop.lock tests/bench/app/bench-matrix.sh [groups…]
#
# Groups (default: all): startup-demo startup-big (startup-big-short) open-page typing select-copy search
#   switcher views journal-scroll switch-pages idle workload
# Labels run alternately per group (base, cur, base, cur … when ROUNDS > 1) to spread drift.
# RUN_TIMEOUT: seconds one app run may take (default 300; a scenario's own --timeout wins);
# BENCH_TIMEOUT_MS (exported) is the probe's watchdog inside the app (default 240000).
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
: "${BINS:?BINS=\"label=binary …\"}" "${PROBE:?}" "${VAULT_DEMO:?}" "${VAULT_BIG:?}" "${OUT:?}"
platform="${PLATFORM:-xvfb}"
runs="${RUNS:-5}"
rounds="${ROUNDS:-1}"
pages="${PAGES:-${VAULT_BIG%/}.pages.txt}"
work="${WORKDIR:-$(mktemp -d)}"
mkdir -p "$OUT" "$work"
read -r -a bins <<< "$BINS"
if [[ $# -gt 0 ]]; then groups=("$@"); else
  groups=(startup-demo startup-big open-page typing select-copy search switcher views journal-scroll switch-pages idle
    workload); fi

run() { # run <label> <bin> <run-bench args…>
  local label="$1" bin="$2"; shift 2
  python3 "$here/run-bench.py" --bin "$bin" --probe "$PROBE" --label "$label" --out "$OUT" \
    --platform "$platform" --workdir "$work/$label" --timeout "${RUN_TIMEOUT:-300}" "$@"
}

group() { # group <name> : runs every label for that group
  local g="$1" r label bin spec n
  for ((r = 0; r < rounds; r++)); do
    for spec in "${bins[@]}"; do
      label="${spec%%=*}"; bin="${spec#*=}"
      n="$runs"
      case "$g" in
        startup-demo)
          run "$label" "$bin" --vault "$VAULT_DEMO" --scenario startup --name startup-demo-first --start first --runs "$n"
          run "$label" "$bin" --vault "$VAULT_DEMO" --scenario startup --name startup-demo-warm --start warm --runs "$n"
          run "$label" "$bin" --vault "$VAULT_DEMO" --scenario startup --name startup-demo-cold --start cold --runs "$n" ;;
        startup-big)
          run "$label" "$bin" --vault "$VAULT_BIG" --scenario startup --name startup-big-first --start first --runs 3
          run "$label" "$bin" --vault "$VAULT_BIG" --scenario startup --name startup-big-warm --start warm --runs "$n"
          run "$label" "$bin" --vault "$VAULT_BIG" --scenario startup --name startup-big-cold --start cold --runs "$n" ;;
        startup-big-short) # first + warm only (slow machines: the cold start is measured with smaller vaults)
          run "$label" "$bin" --vault "$VAULT_BIG" --scenario startup --name startup-big-first --start first --runs 3
          run "$label" "$bin" --vault "$VAULT_BIG" --scenario startup --name startup-big-warm --start warm --runs "$n" ;;
        open-page)
          run "$label" "$bin" --vault "$VAULT_BIG" --scenario open-page --name open-outline5000 --start first --runs "$n" \
            --env "BENCH_PAGE=Bench/Konspekt 5000.md"
          run "$label" "$bin" --vault "$VAULT_BIG" --scenario open-page --name open-document5000 --start first --runs "$n" \
            --env "BENCH_PAGE=Bench/Dokument 5000.md" ;;
        typing)
          run "$label" "$bin" --vault "$VAULT_BIG" --scenario typing --name typing-small --start first --runs "$n" \
            --env "BENCH_PAGE=Bench/Mała strona.md" --env BENCH_ROW=3
          run "$label" "$bin" --vault "$VAULT_BIG" --scenario typing --name typing-outline5000 --start first --runs "$n" \
            --env "BENCH_PAGE=Bench/Konspekt 5000.md"
          run "$label" "$bin" --vault "$VAULT_BIG" --scenario typing --name typing-document5000 --start first --runs "$n" \
            --env "BENCH_PAGE=Bench/Dokument 5000.md" ;;
        select-copy)
          run "$label" "$bin" --vault "$VAULT_BIG" --scenario select-copy --name select-copy-outline5000 --start first \
            --runs "$n" --env "BENCH_PAGE=Bench/Konspekt 5000.md"
          run "$label" "$bin" --vault "$VAULT_BIG" --scenario select-copy --name select-copy-document5000 --start first \
            --runs "$n" --env "BENCH_PAGE=Bench/Dokument 5000.md" ;;
        search) run "$label" "$bin" --vault "$VAULT_BIG" --scenario search --name search-10k --start first --runs "$n" ;;
        switcher) run "$label" "$bin" --vault "$VAULT_BIG" --scenario switcher --name switcher-10k --start first --runs "$n" ;;
        views) run "$label" "$bin" --vault "$VAULT_BIG" --scenario views --name views-10k --start first --runs "$n" ;;
        journal-scroll) run "$label" "$bin" --vault "$VAULT_BIG" --scenario journal-scroll --name journal-scroll-10k --start first --runs "$n" ;;
        switch-pages) run "$label" "$bin" --vault "$VAULT_BIG" --scenario switch-pages --name switch-pages-10k --start first \
            --runs 3 --env "BENCH_PAGES=$(realpath "$pages")" --timeout 600 ;;
        idle)
          run "$label" "$bin" --vault "$VAULT_DEMO" --scenario idle --name idle-demo --start first --runs 3 --env BENCH_IDLE_MS=20000
          run "$label" "$bin" --vault "$VAULT_BIG" --scenario idle --name idle-10k --start first --runs 3 --env BENCH_IDLE_MS=20000
          run "$label" "$bin" --vault "$VAULT_BIG" --scenario idle-edit --name idle-edit-small --start first --runs 3 \
            --env BENCH_IDLE_MS=20000 --env "BENCH_PAGE=Bench/Mała strona.md" ;;
        workload) run "$label" "$bin" --vault "$VAULT_BIG" --scenario workload --name workload-10k --start first --runs 2 \
            --env "BENCH_PAGES=$(realpath "$pages")" --timeout 900 ;;
        *) echo "bench-matrix: unknown group $g" >&2 ;;
      esac
    done
  done
}

for g in "${groups[@]}"; do
  echo "=== $g $(date -Is) load: $(cut -d' ' -f1-3 /proc/loadavg)" | tee -a "$OUT/matrix.log" >&2
  if [[ -n "${LOCK:-}" ]]; then
    # The child runs one group without LOCK (it already holds the lock); "$LOCK" is expanded here.
    # shellcheck disable=SC1007,SC2097,SC2098
    LOCK= WORKDIR="$work" flock -x "$LOCK" "$0" "$g"
  else
    group "$g"
  fi
done
[[ -z "${LOCK:-}" && $# -eq 1 ]] || echo "=== done $(date -Is)" | tee -a "$OUT/matrix.log" >&2
