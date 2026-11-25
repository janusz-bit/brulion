#!/usr/bin/env bash
# tst_startup_order.sh <scripts/startup-order.py> <the built app>
# The order of the app's functions in the program (cmake/startup-order.txt) still names functions
# this build has: a file left behind by the sources orders nothing (scripts/startup-order.py
# writes a new one). 77: no python3 or no nm here.
set -u
script="$1"; app="$2"
command -v python3 >/dev/null 2>&1 || { echo "no python3: skipped"; exit 77; }
command -v nm >/dev/null 2>&1 || { echo "no nm: skipped"; exit 77; }
[[ -f "$app" ]] || { echo "the app is not built ($app): skipped"; exit 77; }
if ! python3 "$script" --check "$app"; then
    echo "FAIL: fewer than half of the names of cmake/startup-order.txt are in this build —"
    echo "      write it again: nix develop -c scripts/startup-order.py --bin <an optimised build of the app>"
    exit 1
fi
