#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Tests scripts/dev-shell.sh (the flake's dev shell from a saved profile) with a stand-in `nix`
# that records how it is called, in a throwaway tree and cache folder: the flake is evaluated once
# for a set of the files that make the shell, a command and its arguments arrive as they are, and
# profiles nobody uses are removed.
#
# Usage: tests/scripts/tst_dev_shell.sh [path/to/dev-shell.sh]   (default: scripts/dev-shell.sh)
set -euo pipefail

script=$(realpath -- "${1:-$(dirname -- "${BASH_SOURCE[0]}")/../../scripts/dev-shell.sh}")
[[ -f $script ]] || {
  echo "no $script" >&2
  exit 2
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

# A tree with the files the shell is made from, and the script in its scripts/.
tree=$tmp/tree
mkdir -p "$tree/scripts" "$tree/nix" "$tree/3rdparty/llama"
cp -- "$script" "$tree/scripts/dev-shell.sh"
echo '{ }' >"$tree/flake.nix"
echo '{ "nodes": 1 }' >"$tree/flake.lock"
echo '{ }' >"$tree/nix/package.nix"
echo '{ }' >"$tree/nix/clang-release.json"
echo '{ "tag": "v1" }' >"$tree/3rdparty/llama/pin.json"

# The stand-in: one line of the log per call, its arguments separated by "|"; a call with
# --profile makes the profile, as `nix develop --profile` does.
mkdir -p "$tmp/bin"
# This bash, not /usr/bin/env (the Nix build sandbox has none).
printf '#!%s\n' "$BASH" >"$tmp/bin/nix"
cat >>"$tmp/bin/nix" <<'SH'
line=""
for a in "$@"; do line+="$a|"; done
printf '%s\n' "$line" >>"$NIX_LOG"
while (($#)); do
  if [[ $1 == --profile ]]; then
    : >"$2"
    shift
  fi
  shift
done
SH
chmod +x "$tmp/bin/nix"
export PATH="$tmp/bin:$PATH" NIX_LOG="$tmp/log" XDG_CACHE_HOME="$tmp/cache"
base=$XDG_CACHE_HOME/brulion/dev-shell

# run <arguments of dev-shell.sh...>: sets $status and $calls (the stand-in's log of this run).
run() {
  : >"$NIX_LOG"
  status=0
  "$BASH" "$tree/scripts/dev-shell.sh" "$@" >"$tmp/out" 2>&1 || status=$?
  calls=$(<"$NIX_LOG")
}
count() { grep -c -- "$1" <<<"$calls" || true; }
# profiles <shell>: how many profiles of that dev shell there are.
profiles() {
  local n=0 d
  for d in "$base/$1"-*/; do
    [[ -d $d ]] && n=$((n + 1))
  done
  echo "$n"
}

run true
expect "the first call succeeds" test "$status" = 0
expect "… it evaluates the flake's default shell into a profile" \
  grep -qE "develop\|$tree#default\|--profile\|$base/default-[0-9a-f]{16}/profile\|-c\|true\|" <<<"$calls"
expect "… and runs the command in that profile" \
  grep -qE "develop\|$base/default-[0-9a-f]{16}/profile\|-c\|true\|$" <<<"$calls"
first=$(ls "$base")

run echo "dwa słowa" 'ąćęłńóśźż' --flag
expect "the second call evaluates nothing" test "$(count '--profile')" = 0
expect "… and hands the command its arguments as they are" \
  grep -qF -- '|-c|echo|dwa słowa|ąćęłńóśźż|--flag|' <<<"$calls"
expect "… in the same profile" test "$(ls "$base")" = "$first"

run
expect "without a command it opens the shell" \
  grep -qE "develop\|$base/default-[0-9a-f]{16}/profile\|$" <<<"$calls"

run -s cli true
expect "-s cli evaluates devShells.cli" grep -qF -- "develop|$tree#cli|--profile|" <<<"$calls"
expect "… into a profile of its own" test "$(profiles cli)" = 1

echo '{ "nodes": 2 }' >"$tree/flake.lock"
run true
expect "another flake.lock is another evaluation" test "$(count '--profile')" = 1
expect "… into another profile" test "$(profiles default)" = 2

echo '{ "tag": "v2" }' >"$tree/3rdparty/llama/pin.json"
run true
expect "another pin of llama.cpp too" test "$(count '--profile')" = 1

echo 'int main() { }' >"$tree/main.cpp"
run true
expect "a changed source file is none" test "$(count '--profile')" = 0

# Profiles: one used 40 days ago goes, one used yesterday stays.
mkdir -p "$base/default-0000000000000000" "$base/default-1111111111111111"
touch -d '40 days ago' "$base/default-0000000000000000/used"
touch -d 'yesterday' "$base/default-1111111111111111/used"
run true
expect "a profile not used for 30 days is removed" test ! -e "$base/default-0000000000000000"
expect "… one used yesterday is kept" test -d "$base/default-1111111111111111"

run -s 'bad name' true
expect "a name that is none is refused" test "$status" = 2
expect "… before anything is evaluated" test -z "$calls"

run -s
expect "-s without a name is refused" test "$status" = 2

# A profile whose store path is gone (a dangling link) is evaluated again.
run true
profile=$(grep -oE "$base/default-[0-9a-f]{16}/profile" <<<"$calls" | head -n 1)
rm -f -- "$profile"
ln -s "$tmp/nowhere" "$profile"
run true
expect "a profile whose store path is gone is evaluated again" test "$(count '--profile')" = 1

if ((failed)); then
  echo "Totals: $failed failed"
  exit 1
fi
echo "Totals: all passed"
