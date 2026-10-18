#!/usr/bin/env bash
# Wait until the machine is quiet enough for timings: the CPU busy share over 5 s below
# <max busy %> (default 12 % of all cores). Gives up after <max wait s> (default 1800) and
# says so (exit 1). Prints the measured busy share.
#   tests/bench/app/wait-quiet.sh [max_busy_pct] [max_wait_s]
max="${1:-12}"; deadline=$(( $(date +%s) + ${2:-1800} ))
busy() {
  read -r _ a b c d e f g h _ < /proc/stat; t1=$((a+b+c+d+e+f+g+h)); i1=$((d+e))
  sleep 5
  read -r _ a b c d e f g h _ < /proc/stat; t2=$((a+b+c+d+e+f+g+h)); i2=$((d+e))
  echo $(( 100 * ((t2-t1) - (i2-i1)) / (t2-t1) ))
}
while :; do
  b=$(busy)
  if (( b < max )); then echo "wait-quiet: busy ${b}% ($(cut -d' ' -f1-3 /proc/loadavg))"; exit 0; fi
  if (( $(date +%s) > deadline )); then echo "wait-quiet: still busy ${b}% — measuring anyway is not advised" >&2; exit 1; fi
  echo "wait-quiet: busy ${b}%, waiting" >&2
  sleep 25
done
