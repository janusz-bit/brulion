#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Recompute the hash of the npm dependencies in nix/npm-deps.nix from package-lock.json.
# Run it after every change to package-lock.json (added or updated dependencies, and also a
# version bump: the hash covers the whole lock file), then commit both files together.
#
# Usage: scripts/update-nix-hashes.sh [--check]
#   --check  only compare: exit 1 when nix/npm-deps.nix is out of date (CI, pre-commit)
# Exit status 2: the hash could not be computed (no nix, network, unexpected file layout).
#
# Dependabot's npm PRs get this commit from .github/workflows/nix-hash.yml.
#
# The hash comes from prefetch-npm-deps of the nixpkgs pinned in flake.lock, the same tool
# fetchNpmDeps runs in the build. Cargo needs no hash: nix/package.nix vendors the crates
# straight from src-tauri/Cargo.lock (only git dependencies would need `outputHashes`).
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

FILE=nix/npm-deps.nix
HASH_LINE='^  hash = "sha256-[A-Za-z0-9+/]*=*";$'

die() { printf 'error: %s\n' "$*" >&2; exit 2; }

check=0
for arg in "$@"; do
  case $arg in
    --check) check=1 ;;
    -h | --help)
      awk 'NR > 2 && /^#/ { sub(/^# ?/, ""); print; next } NR > 2 { exit }' "${BASH_SOURCE[0]}"
      exit 0
      ;;
    *) die "unknown argument $arg (see --help)" ;;
  esac
done

command -v nix >/dev/null 2>&1 || die "nix is not installed"
[[ -f package-lock.json ]] || die "package-lock.json not found"
[[ $(grep -c -- "$HASH_LINE" "$FILE") == 1 ]] || die "expected exactly one 'hash = \"sha256-...\";' line in $FILE"

current=$(sed -n 's/^  hash = "\(sha256-[^"]*\)";$/\1/p' "$FILE")

echo "Fetching the npm dependencies of package-lock.json (prefetch-npm-deps)..." >&2
# NPM_FETCHER_VERSION must match `fetcherVersion` of fetchNpmDeps (default 1).
new=$(
  NPM_FETCHER_VERSION=1 nix --extra-experimental-features 'nix-command flakes' \
    run --inputs-from . nixpkgs#prefetch-npm-deps -- package-lock.json
) || die "prefetch-npm-deps failed (see above)"
new=${new##*$'\n'}
[[ $new =~ ^sha256-[A-Za-z0-9+/]+=*$ ]] || die "unexpected output of prefetch-npm-deps: $new"

if [[ $new == "$current" ]]; then
  echo "$FILE is up to date ($new)."
  exit 0
fi

if ((check)); then
  printf '%s is out of date (package-lock.json changed):\n  has:    %s\n  should: %s\n' \
    "$FILE" "$current" "$new" >&2
  printf 'Run scripts/update-nix-hashes.sh and commit %s together with package-lock.json.\n' \
    "$FILE" >&2
  exit 1
fi

sed -i "s|^  hash = \"sha256-[^\"]*\";$|  hash = \"$new\";|" "$FILE"
echo "Updated $FILE: $current -> $new"
