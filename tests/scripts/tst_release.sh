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
  work=$root/work origin=$root/origin.git
}

# other <name>: a second clone of the fixture's origin (another machine), on main.
other() {
  git clone -q "$origin" "$tmp/$1"
  other=$tmp/$1
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

echo "--- a release branch that lacks main's commits is refused"
fixture stale
git -C "$work" switch -q main
commit "$work" fix.txt 'poprawka' "Fix on main"
git -C "$work" switch -q release
head=$(sha "$work" HEAD)
rel "$work" patch --yes --push
expect "a real run fails" test "$status" != 0
expect "... names main and how to merge it" has "main: 1 commit (git merge main)"
expect "... mentions --allow-stale" has "--allow-stale"
expect "... creates no tag" test "$(git -C "$work" tag -l v0.3.6)" = ""
expect "... and no commit" test "$(sha "$work" HEAD)" = "$head"
expect "... and pushes nothing" same "$origin" release "$work" HEAD
rel "$work" --dry-run patch
expect "a dry run warns about it" has "A real run refuses to release without them"
expect "... and still shows the plan" has "Brulion 0.3.5 -> 0.3.6 (tag v0.3.6, from release)"
git -C "$work" switch -q main
rel "$work" --dry-run patch
expect "a dry run from main checks the release branch" has "'release' lacks commits that are on"

echo "--- ... also when the commits are only on origin/main (not fetched yet)"
git -C "$work" switch -q release
git -C "$work" merge -q --no-edit main
other stale-other
commit "$other" fix2.txt 'druga poprawka' "Fix pushed from elsewhere"
git -C "$other" push -q origin main
rel "$work" patch --yes
expect "a real run fails" test "$status" != 0
expect "... naming origin/main and git fetch" has "origin/main: commits not fetched yet (git fetch origin && git merge origin/main)"
expect "... not main, which is merged" test "$(grep -c '^  main:' <<<"$out")" = 0
git -C "$work" fetch -q origin
rel "$work" patch --yes
expect "fetched but not merged: origin/main, 1 commit" has "origin/main: 1 commit (git fetch origin && git merge origin/main)"

echo "--- ... and when origin/release moved on"
git -C "$work" merge -q --no-edit origin/main
git -C "$other" switch -q release
commit "$other" .github/release-notes.md '* Inna notatka' "Notes from elsewhere"
git -C "$other" push -q origin release
rel "$work" patch --yes
expect "a real run fails" test "$status" != 0
expect "... naming origin/release" has "origin/release: commits not fetched yet (git fetch origin && git merge origin/release)"

echo "--- --allow-stale releases anyway"
rel "$work" patch --yes --allow-stale
expect "the run succeeds" test "$status" = 0
expect "... with a warning" has "Releasing without them (--allow-stale)."
expect "... and the tag" test "$(git -C "$work" tag -l v0.3.6)" = v0.3.6

echo "--- unchanged release notes are pointed out"
fixture notes
rel "$work" --dry-run patch
expect "notes unchanged since v0.3.5" has ".github/release-notes.md has not changed since v0.3.5"
commit "$work" .github/release-notes.md '* Nowość (0.3.6)' "Update release notes for v0.3.6"
rel "$work" --dry-run patch
expect "changed notes: no warning" test "$(grep -c 'has not changed' <<<"$out")" = 0

echo "--- a release of the recorded version pushes the branch with the tag"
fixture rc
commit "$work" .github/release-notes.md '* Wydanie testowe' "Update release notes for v0.4.0-rc.1"
rel "$work" 0.4.0-rc.1 --yes --push
expect "rc.1 bumps and pushes" same "$origin" release "$work" HEAD
expect "... with its tag" same "$origin" v0.4.0-rc.1 "$work" HEAD
git -C "$work" switch -q main
commit "$work" fix.txt 'poprawka do rc' "Fix for rc.2"
git -C "$work" push -q origin main
git -C "$work" switch -q release
git -C "$work" merge -q --no-edit main
commit "$work" .github/release-notes.md '* Drugie wydanie testowe' "Update release notes for v0.4.0-rc.2"
rel "$work" 0.4.0-rc.2 --yes --push
expect "rc.2 only tags" has "CMakeLists.txt already has 0.4.0: only the tag is created."
expect "... and pushes the branch too" same "$origin" release "$work" HEAD
expect "... with the tag on it" same "$origin" v0.4.0-rc.2 "$origin" release
expect "the tag is on a branch of origin" test -n "$(git -C "$origin" branch --contains v0.4.0-rc.2)"
commit "$work" .github/release-notes.md '* Trzecie' "Update release notes for v0.4.0-rc.3"
rel "$work" 0.4.0-rc.3 --yes
expect "without --push: the hint pushes the branch with the tag" has "git push --atomic origin release v0.4.0-rc.3"

echo
if ((failed)); then
  echo "Totals: $failed failed"
  exit 1
fi
echo "Totals: all passed"
