#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Tests scripts/release.sh in throwaway repositories: a local bare repository is origin, so
# nothing goes over the network. Each case starts from the state after v0.3.5 was released on
# the release branch while main still says 0.3.4.
#
# Usage: tests/scripts/tst_release.sh [path/to/release.sh]   (default: scripts/release.sh)
set -euo pipefail

script=$(realpath -- "${1:-$(dirname -- "${BASH_SOURCE[0]}")/../../scripts/release.sh}")
[[ -f $script ]] || {
  echo "no $script" >&2
  exit 2
}
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT

# Only this test's git settings: no user configuration (signing, hooks, default branch).
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
export GIT_AUTHOR_NAME=Tester GIT_AUTHOR_EMAIL=tester@example.invalid
export GIT_COMMITTER_NAME=Tester GIT_COMMITTER_EMAIL=tester@example.invalid
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE

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
has() { grep -qF -- "$1" <<<"$out"; } # the last run printed <text>
first_line_has() { grep -qF -- "$1" <<<"${out%%$'\n'*}"; }
sha() { git -C "$1" rev-parse -q --verify "$2^{commit}" 2>/dev/null || echo none; }
same() { [[ $(sha "$1" "$2") == $(sha "$3" "$4") ]]; } # <repo> <rev> <repo> <rev>
version_on() { git -C "$1" show "$2:CMakeLists.txt" | sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9.]+)$/\1/p'; }

# rel <clone> <arguments...>: runs release.sh there; sets $status and $out (stdout + stderr).
rel() {
  local dir=$1
  shift
  status=0
  out=$(cd -- "$dir" && bash scripts/release.sh "$@" </dev/null 2>&1) || status=$?
  printf '%s\n' "$out" | sed 's/^/        | /'
}

commit() { # <clone> <file> <text> <message>
  printf '%s\n' "$3" >>"$1/$2"
  git -C "$1" add -- "$2"
  git -C "$1" commit -q -m "$4"
}

# fixture <name>: $tmp/<name>/origin.git and a clone $tmp/<name>/work on release.
fixture() {
  local root=$tmp/$1 seed=$tmp/$1/seed
  mkdir -p "$seed/scripts" "$seed/nix" "$seed/.github"
  cat >"$seed/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.24)

project(Brulion
    VERSION 0.3.4
    LANGUAGES CXX)
EOF
  cat >"$seed/nix/package.nix" <<'EOF'
{ stdenv }:
stdenv.mkDerivation {
  pname = "brulion";
  version = "0.3.4";
}
EOF
  printf '%s\n' '**Brulion {{VERSION}}** — notatki' '' '* Zażółć gęślą jaźń (0.3.4)' >"$seed/.github/release-notes.md"
  cp -- "$script" "$seed/scripts/release.sh"
  git init -q -b main "$seed"
  git -C "$seed" add -A
  git -C "$seed" commit -q -m "Initial"
  git -C "$seed" tag -a v0.3.4 -m "Brulion 0.3.4"
  # v0.3.5 cut on release and never merged back into main.
  git -C "$seed" switch -q -c release
  commit "$seed" .github/release-notes.md '* Zegar (0.3.5)' "Update release notes for v0.3.5"
  sed -i 's/0\.3\.4/0.3.5/' "$seed/CMakeLists.txt" "$seed/nix/package.nix"
  git -C "$seed" commit -q -am "Release v0.3.5"
  git -C "$seed" tag -a v0.3.5 -m "Brulion 0.3.5"
  git init -q --bare -b main "$root/origin.git"
  git -C "$seed" push -q "$root/origin.git" main release --tags
  git clone -q "$root/origin.git" "$root/work"
  git -C "$root/work" switch -q release
  work=$root/work
}

echo "--- a dry run from main shows what a real run on release would do"
fixture dry
git -C "$work" switch -q main
rel "$work" --dry-run patch
expect "dry run from main succeeds" test "$status" = 0
expect "the branch warning comes first" first_line_has "releases go on the 'release' branch, not 'main'"
expect "versions come from release (0.3.5 -> 0.3.6)" has "Brulion 0.3.5 -> 0.3.6 (tag v0.3.6, from release)"
rel "$work" --dry-run minor
expect "minor from main: 0.3.5 -> 0.4.0" has "Brulion 0.3.5 -> 0.4.0 (tag v0.4.0, from release)"
git -C "$work" branch -q -D release
rel "$work" -n patch
expect "without a local release branch: origin/release" has "Brulion 0.3.5 -> 0.3.6 (tag v0.3.6, from origin/release)"
rel "$work" patch --yes
expect "a real run from main fails" test "$status" != 0
expect "... with the branch message, not a tag error" first_line_has "error: releases go on the 'release' branch, not 'main'"
expect "... and creates no tag" test "$(git -C "$work" tag -l 'v0.3.6')" = ""

echo
if ((failed)); then
  echo "Totals: $failed failed"
  exit 1
fi
echo "Totals: all passed"
