#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The flake's dev shell from a saved profile. `nix develop` evaluates the flake on every call —
# from a tree with uncommitted changes it first copies the tree into the Nix store and evaluates
# nixpkgs again: 15–30 s on the Raspberry Pi, every time, and one more copy of the sources in the
# store — and nothing keeps the shell's packages from the garbage collector (after a collection the
# compiler is downloaded and Qt Multimedia compiled again). This script evaluates once for a set of
# the files that make the shell (flake.nix, flake.lock, nix/, 3rdparty/*/pin.json), saves the
# result as a Nix profile under $XDG_CACHE_HOME/brulion/dev-shell — a garbage collector root,
# shared by every checkout and worktree of this user — and enters that: half a second
# (docs/PERFORMANCE.md "What Nix costs").
#
#   scripts/dev-shell.sh                                              # an interactive shell
#   scripts/dev-shell.sh scripts/dev-build.sh -t tst_dates tst_dates  # a command in the shell
#   scripts/dev-shell.sh -s cli scripts/dev-build.sh -t tst_dates tst_dates
#       devShells.cli: the core, brulion-cli and their tests alone (Qt's base; no Qt Quick, no Qt
#       Multimedia to compile first) — the light shell for the Raspberry Pi; builds into ./build-cli
#
# A profile not used for 30 days is removed, and its packages with the next garbage collection.
# Needs Nix (flakes) and sha256sum.
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)

die() {
  printf 'dev-shell: %s\n' "$*" >&2
  exit 2
}

shell=default
case ${1:-} in
  -h | --help)
    awk 'NR > 2 && /^#/ { sub(/^# ?/, ""); print; next } NR > 2 { exit }' "${BASH_SOURCE[0]}"
    exit 0
    ;;
  -s)
    [[ $# -ge 2 ]] || die "-s takes the name of a dev shell (default, cli)"
    shell=$2
    shift 2
    ;;
esac
[[ $shell =~ ^[A-Za-z0-9_-]+$ ]] || die "not the name of a dev shell: $shell"

# What the shell is made from: another key, another evaluation.
key=$(
  cd "$ROOT"
  {
    echo "$shell"
    sha256sum flake.nix flake.lock nix/*.nix nix/*.json 3rdparty/*/pin.json
  } | sha256sum | cut -c1-16
)
base=${XDG_CACHE_HOME:-$HOME/.cache}/brulion/dev-shell
dir=$base/$shell-$key
profile=$dir/profile

# (-e follows the link: a profile whose store path is gone is made again)
if [[ ! -e $profile ]]; then
  mkdir -p "$dir"
  echo "dev-shell: evaluating devShells.$shell (once for this flake.nix, flake.lock and nix/)" >&2
  nix --extra-experimental-features 'nix-command flakes' develop "$ROOT#$shell" --profile "$profile" -c true
fi
touch "$dir/used"

for other in "$base"/*/; do
  [[ -d $other && ! $other -ef $dir ]] || continue
  if [[ -z $(find "$other" -maxdepth 1 -name used -mtime -30 2>/dev/null) ]]; then
    rm -rf -- "$other"
  fi
done

if [[ $# -eq 0 ]]; then
  exec nix --extra-experimental-features 'nix-command flakes' develop "$profile"
fi
exec nix --extra-experimental-features 'nix-command flakes' develop "$profile" -c "$@"
