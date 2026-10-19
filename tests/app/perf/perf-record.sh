#!/usr/bin/env bash
# CPU profile (perf) of one scroll-probe run, for finding what a stutter spends its time on:
#   tests/app/perf/perf-record.sh <out.data> <app args…>
#   e.g. tests/app/perf/perf-record.sh /tmp/doc.data --route '{"view":"page","path":"Witaj w Brulionie.md"}' \
#          --size 1280x800 --scroll-probe auto --scroll-script touchpad
# A fresh temporary XDG home with the sample notebook (VAULT=big: make-vault.py's 365 days);
# Xvfb when xvfb-run exists (nix shell nixpkgs#xvfb-run nixpkgs#perf); PROBE_XDG_DATA_DIRS as in
# scroll-probe.sh. Report: perf report -i <out.data> --no-children --comm brulion --sort symbol
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.."
out="$1"; shift
bin="${BRULION_BIN:-build/src/app/brulion}"
now="${NOW:-2026-10-14 10:00}"
home="$(mktemp -d)"
trap 'rm -rf "$home"' EXIT
export XDG_CONFIG_HOME="$home/config" XDG_DATA_HOME="$home/data" XDG_CACHE_HOME="$home/cache" XDG_STATE_HOME="$home/state"
[[ -n "${PROBE_XDG_DATA_DIRS:-}" ]] && export XDG_DATA_DIRS="$PROBE_XDG_DATA_DIRS"
wrap=()
if command -v xvfb-run >/dev/null 2>&1; then wrap=(xvfb-run -a -s "-screen 0 1920x1200x24"); else export QT_QPA_PLATFORM=offscreen; fi
if [[ "${VAULT:-demo}" == big ]]; then
  vault="$home/Duży brulion"
  python3 tests/app/perf/make-vault.py "$vault" --days "${DAYS:-365}" --today "${now%% *}" >&2
else
  vault="$home/Przykładowy brulion"
fi
"${wrap[@]}" "$bin" --no-single-instance --demo --vault "$vault" --size 800x600 --now "$now" --delay 1500 \
  --screenshot "$home/prep.png" >/dev/null 2>&1 || true
cp "tests/app/shell/notemode/Wyprawa w Bieszczady.md" "$vault/"
"${wrap[@]}" perf record -F "${FREQ:-1000}" -g --call-graph "${CALLGRAPH:-dwarf,32768}" -o "$out" -- \
  "$bin" --no-single-instance --vault "$vault" --locale pl --now "$now" "$@"
