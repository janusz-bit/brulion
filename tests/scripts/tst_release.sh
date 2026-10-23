#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Tests scripts/release.sh in throwaway repositories: a local bare repository is origin, so
# nothing goes over the network. Each case starts from the state after the switch to releases
# made from main: v0.3.4 was cut on main, v0.3.5 on the release branch (its notes and version
# commits only there), then main took them over and moved on. From there on main has every
# commit and release is a commit of main (an ancestor of main, or main itself) that each release
# moves forward; the tag goes on that commit, the same on both branches.
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
lacks() { ! has "$1"; }
succeeds_with() { [[ $status == 0 ]] && has "$1"; } # the last run succeeded and printed <text>
first_line_has() { grep -qF -- "$1" <<<"${out%%$'\n'*}"; }
sha() { git -C "$1" rev-parse -q --verify "$2^{commit}" 2>/dev/null || echo none; }
same() { [[ $(sha "$1" "$2") == $(sha "$3" "$4") ]]; } # <repo> <rev> <repo> <rev>
subject() { git -C "$1" log -1 --format=%s "$2"; }
short() { git -C "$1" rev-parse --short "$2"; }
# release has nothing but commits of main: git rev-list main..release is empty.
release_in_main() { [[ -z $(git -C "$1" rev-list main..release) ]]; }
no_merge_since() { [[ -z $(git -C "$1" rev-list --merges "$2..release") ]]; } # <repo> <rev>
ancestor() { git -C "$1" merge-base --is-ancestor "$2" "$3"; }                # <repo> <old> <new>
version_on() { git -C "$1" show "$2:CMakeLists.txt" | sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9.]+)$/\1/p'; }
nix_version_on() { git -C "$1" show "$2:nix/package.nix" | sed -nE 's/^[[:space:]]*version = "([^"]+)";/\1/p'; }
no_tag() { [[ -z $(git -C "$1" tag -l "$2") ]]; }
# What a run must not change: the branches, the tags and the working tree (a real run may fetch,
# so the remote-tracking branches are left out; state also has those and HEAD).
snapshot() {
  git -C "$1" for-each-ref --format='%(refname) %(objectname)' refs/heads refs/tags
  git -C "$1" status --porcelain
}
state() {
  git -C "$1" for-each-ref --format='%(refname) %(objectname)'
  git -C "$1" symbolic-ref -q HEAD || git -C "$1" rev-parse HEAD
  git -C "$1" status --porcelain
}
origin_refs() { git -C "$origin" for-each-ref --format='%(refname) %(objectname)'; }
on_main_clean() { # HEAD on main, no change in the working tree, no merge in progress
  [[ $(git -C "$1" symbolic-ref -q HEAD) == refs/heads/main && -z $(git -C "$1" status --porcelain) ]] &&
    ! git -C "$1" rev-parse -q --verify MERGE_HEAD >/dev/null
}

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

hook() { # <clone or bare repository> <name> <bash code>: installs that git hook
  local dir=$1/.git/hooks
  [[ -d $1/.git ]] || dir=$1/hooks
  mkdir -p -- "$dir"
  printf '#!/usr/bin/env bash\n%s\n' "$3" >"$dir/$2"
  chmod +x "$dir/$2"
}
bump_version() { # <clone> <version>: commits "Release v<version>" by hand (CMake + nix)
  sed -i -E "s/^( *VERSION )[0-9.]+$/\1$2/; s/^( *version = \")[^\"]+/\1$2/" "$1/CMakeLists.txt" "$1/nix/package.nix"
  git -C "$1" commit -q -am "Release v$2"
}

# fixture <name> [legacy]: $tmp/<name>/origin.git and a clone $tmp/<name>/work on main, with a
# local release branch; $start is release's commit (v0.3.5). With "legacy" main never took
# v0.3.5 over (the state before the switch).
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
  git -C "$seed" switch -q -c release
  commit "$seed" .github/release-notes.md '* Zegar (0.3.5)' "Update release notes for v0.3.5"
  sed -i 's/0\.3\.4/0.3.5/' "$seed/CMakeLists.txt" "$seed/nix/package.nix"
  git -C "$seed" commit -q -am "Release v0.3.5"
  git -C "$seed" tag -a v0.3.5 -m "Brulion 0.3.5"
  git -C "$seed" switch -q main
  [[ ${2-} == legacy ]] || git -C "$seed" merge -q --ff-only release
  commit "$seed" fix.txt 'poprawka' "Fix on main"
  git init -q --bare -b main "$root/origin.git"
  git -C "$seed" push -q "$root/origin.git" main release --tags
  git clone -q "$root/origin.git" "$root/work"
  git -C "$root/work" branch -q --track release origin/release
  work=$root/work origin=$root/origin.git
  start=$(sha "$work" release)
}

# other <name>: a second clone of the fixture's origin (another machine), on main, without a
# local release branch.
other() {
  git clone -q "$origin" "$tmp/$1"
  other=$tmp/$1
}

echo "--- a release from main: the version on main, release fast-forwarded to it, the tag on it"
fixture normal
commit "$work" .github/release-notes.md '* Poprawka (0.3.6)' "Update release notes for v0.3.6"
main0=$(sha "$work" main) origin0=$(origin_refs)
rel "$work" patch --yes
expect "the release succeeds" test "$status" = 0
expect "main gets the commit \"Release v0.3.6\"" test "$(subject "$work" main)" = "Release v0.3.6"
expect "... on top of main" test "$(sha "$work" main^)" = "$main0"
expect "... with 0.3.6 in CMakeLists.txt" test "$(version_on "$work" main)" = 0.3.6
expect "... and in nix/package.nix" test "$(nix_version_on "$work" main)" = 0.3.6
expect "release is that very commit of main" same "$work" release "$work" main
expect "... reached by a fast-forward from v0.3.5" ancestor "$work" "$start" release
expect "... without a merge" no_merge_since "$work" "$start"
expect "git rev-list main..release is empty" release_in_main "$work"
expect "it says so" has "release: fast-forwards from $(short "$work" "$start") to main's \"Release v0.3.6\" (3 commits of main) and tags it v0.3.6"
expect "the tag is annotated" test "$(git -C "$work" cat-file -t v0.3.6)" = tag
expect "... and on that commit, main's and release's" same "$work" v0.3.6 "$work" main
expect "... as the run says" has "Released v0.3.6: main and release at $(short "$work" main) \"Release v0.3.6\"."
expect "the checkout stays on main, clean" on_main_clean "$work"
expect "without --push nothing is pushed" test "$(origin_refs)" = "$origin0"
expect "it prints the atomic push" has "Publish it with:  git push --atomic origin main release refs/tags/v0.3.6"
undo=$(sed -n 's/^Undo it (before pushing) with:  //p' <<<"$out")
expect "... and how to undo the release" test -n "$undo"
(cd -- "$work" && eval "$undo") >/dev/null
expect "that undoes it: main" test "$(sha "$work" main)" = "$main0"
expect "... release" test "$(sha "$work" release)" = "$start"
expect "... and the tag" no_tag "$work" v0.3.6
expect "... and leaves a clean main" on_main_clean "$work"

echo "--- --push pushes main, release and the tag atomically"
rel "$work" patch --yes --push
expect "the release succeeds" test "$status" = 0
expect "origin's main is main with the version commit" same "$origin" main "$work" main
expect "origin's release is the same commit" same "$origin" release "$work" main
expect "origin has the tag (the same tag object)" test "$(git -C "$origin" rev-parse v0.3.6)" = "$(git -C "$work" rev-parse v0.3.6)"
expect "... on that commit" same "$origin" v0.3.6 "$origin" main
expect "on origin too, git rev-list main..release is empty" release_in_main "$origin"
expect "origin/main and origin/release follow" test "$(sha "$work" origin/main)/$(sha "$work" origin/release)" = "$(sha "$work" main)/$(sha "$work" release)"
expect "main on origin has the released version" test "$(version_on "$origin" main)" = 0.3.6

echo "--- release after release: release only moves forward along main"
commit "$work" fix.txt 'druga poprawka' "Second fix on main"
commit "$work" .github/release-notes.md '* Druga (0.3.7)' "Update release notes for v0.3.7"
rel "$work" patch --yes --push
expect "v0.3.7 is released" test "$status/$(sha "$origin" v0.3.7)" = "0/$(sha "$work" release)"
commit "$work" feature.txt 'nowość' "Feature on main"
commit "$work" .github/release-notes.md '* Nowa funkcja (0.4.0)' "Update release notes for v0.4.0"
rel "$work" minor --yes --push
expect "v0.4.0 is released" test "$status/$(sha "$origin" v0.4.0)" = "0/$(sha "$work" release)"
expect "... from 0.3.7" has "Brulion 0.3.7 -> 0.4.0 (tag v0.4.0, from main)"
expect "release is main" same "$origin" release "$origin" main
expect "git rev-list main..release is empty" release_in_main "$origin"
expect "release has no merge since v0.3.5" no_merge_since "$origin" "$start"
expect "... its log is main's" test "$(git -C "$origin" log --format=%H release)" = "$(git -C "$origin" log --format=%H main)"
for t in v0.3.6 v0.3.7 v0.4.0; do
  expect "$t: on main's \"Release $t\"" test "$(subject "$origin" "$t")" = "Release $t"
  expect "... a commit of main" ancestor "$origin" "$t" main
  expect "... and of release" ancestor "$origin" "$t" release
done
expect "the releases are the tags on release" test "$(git -C "$origin" tag -l 'v*' --merged release --sort=v:refname | paste -sd ' ')" = "v0.3.4 v0.3.5 v0.3.6 v0.3.7 v0.4.0"
expect "main has every release's version commit" test "$(git -C "$origin" log --format=%s --grep='^Release v' main | paste -sd '|')" = "Release v0.4.0|Release v0.3.7|Release v0.3.6|Release v0.3.5"
expect "... and the version" test "$(version_on "$origin" main)" = 0.4.0

echo "--- a real run only on main"
fixture branch
git -C "$work" branch -q topic
before=$(snapshot "$work") origin0=$(origin_refs)
for where in release topic detached; do
  if [[ $where == detached ]]; then git -C "$work" switch -q --detach main; else git -C "$work" switch -q "$where"; fi
  rel "$work" patch --yes --push
  name=$where
  [[ $where != detached ]] || name="(detached)"
  expect "on $where it fails" test "$status" != 0
  expect "... with the branch message first" first_line_has "error: releases are made on main, not '$name' (git switch main)"
  expect "... changes nothing" test "$(snapshot "$work")" = "$before"
  expect "... and pushes nothing" test "$(origin_refs)" = "$origin0"
done

echo "--- a dry run from any branch shows what a real run on main would do, and changes nothing"
fixture dry
commit "$work" .github/release-notes.md '* Nowe (0.3.6)' "Update release notes for v0.3.6"
git -C "$work" branch -q topic release
origin0=$(origin_refs)
for where in main release topic detached; do
  if [[ $where == detached ]]; then git -C "$work" switch -q --detach release; else git -C "$work" switch -q "$where"; fi
  before=$(state "$work")
  rel "$work" --dry-run patch --push
  expect "from $where: it succeeds" test "$status" = 0
  expect "... shows main's versions" has "Brulion 0.3.5 -> 0.3.6 (tag v0.3.6, from main)"
  expect "... the version commit" has 'commits "Release v0.3.6"'
  expect "... the fast-forward and the tag" has "release: fast-forwards from $(short "$work" "$start") to main's \"Release v0.3.6\" (3 commits of main) and tags it v0.3.6"
  expect "... and the push" has "git push --atomic origin main release refs/tags/v0.3.6"
  expect "... refuses nothing" lacks "A real run refuses"
  expect "... and changes nothing" test "$(state "$work")" = "$before"
  if [[ $where == release ]]; then
    expect "... after a warning about the branch" first_line_has "warning: releases are made on main, not 'release'"
  fi
done
expect "origin is untouched" test "$(origin_refs)" = "$origin0"
git -C "$work" switch -q topic
git -C "$work" branch -q -D main
rel "$work" -n minor
expect "without a local main: origin/main's versions" has "Brulion 0.3.5 -> 0.4.0 (tag v0.4.0, from origin/main)"

echo "--- main behind origin/main is refused"
fixture behind
other behind-other
commit "$other" fix2.txt 'z innego komputera' "Fix pushed from elsewhere"
git -C "$other" push -q origin main
before=$(state "$work")
rel "$work" --dry-run patch
expect "a dry run warns: commits not fetched yet" has "main lacks commits of origin/main that are not fetched yet (git pull --ff-only)"
expect "... that a real run refuses" has "A real run refuses to release"
expect "... and changes nothing (no fetch either)" test "$(state "$work")" = "$before"
before=$(snapshot "$work") origin0=$(origin_refs)
rel "$work" patch --yes --push
expect "a real run fetches and fails" test "$status" != 0
expect "... saying what main lacks" has "main lacks 1 commit of origin/main (git pull --ff-only"
expect "... changes nothing" test "$(snapshot "$work")" = "$before"
expect "... and pushes nothing" test "$(origin_refs)" = "$origin0"
git -C "$work" pull -q --ff-only
rel "$work" patch --yes --push
expect "after git pull it releases" test "$status/$(sha "$origin" v0.3.6)" = "0/$(sha "$work" release)"
expect "... with the fix from elsewhere" git -C "$work" merge-base --is-ancestor "$(sha "$other" main)" v0.3.6

echo "--- ... also after a release made elsewhere: main is to be pulled, release is not wrong"
other behind-elsewhere
commit "$other" .github/release-notes.md '* Z innego komputera (0.3.7)' "Update release notes for v0.3.7"
rel "$other" patch --yes --push
expect "the other clone releases v0.3.7" test "$status/$(sha "$origin" v0.3.7)" = "0/$(sha "$origin" release)"
git -C "$work" fetch -q origin # origin's commits are here, main is not pulled yet
before=$(state "$work")
rel "$work" --dry-run patch
expect "a dry run says main lacks origin/main's commits" has "main lacks 2 commits of origin/main (git pull --ff-only"
expect "... not that release has commits of its own" lacks "that main lacks (git rev-list main..release"
expect "... nor to put release back" lacks "git branch -f release"
expect "... and changes nothing" test "$(state "$work")" = "$before"
before=$(snapshot "$work") origin0=$(origin_refs)
rel "$work" patch --yes --push
expect "a real run refuses" has "main lacks 2 commits of origin/main (git pull --ff-only"
expect "... changing nothing" test "$(snapshot "$work")" = "$before"
expect "... and pushing nothing" test "$(origin_refs)" = "$origin0"
git -C "$work" pull -q --ff-only
commit "$work" .github/release-notes.md '* Kolejna (0.3.8)' "Update release notes for v0.3.8"
rel "$work" patch --yes --push
expect "after git pull it releases v0.3.8" test "$status/$(sha "$origin" v0.3.8)" = "0/$(sha "$work" release)"
expect "... moving release on from v0.3.7" ancestor "$work" v0.3.7 release

echo "--- release with a commit of its own is refused, with the way back"
fixture stray
git -C "$work" switch -q release
commit "$work" hotfix.txt 'szybka poprawka' "Hotfix on release"
git -C "$work" switch -q main
commit "$work" .github/release-notes.md '* Poprawka (0.3.6)' "Update release notes for v0.3.6"
before=$(snapshot "$work") origin0=$(origin_refs)
rel "$work" patch --yes --push
expect "a real run fails" test "$status" != 0
expect "... with the rule" has "release has 1 commit that main lacks (git rev-list main..release must be empty):"
expect "... naming the commit" has "Hotfix on release"
expect "... and the way back" has "Put release back at $(short "$work" "$start") \"Release v0.3.5\", the last commit of main it has: git branch -f release $(short "$work" "$start")"
expect "... (origin's release is fine)" lacks "force-with-lease"
expect "... and what to do with a change main needs" has "goes on main first (git cherry-pick <commit> on main)"
expect "... changes nothing" test "$(snapshot "$work")" = "$before"
expect "... and pushes nothing" test "$(origin_refs)" = "$origin0"
rel "$work" --dry-run patch
expect "a dry run says a real run refuses" succeeds_with "A real run refuses"
fix=$(sed -n 's/.*the last commit of main it has: \(git branch -f release [0-9a-f]*\).*/\1/p' <<<"$out")
expect "the way back is a command" test -n "$fix"
(cd -- "$work" && eval "$fix")
expect "... that puts release back at v0.3.5" test "$(sha "$work" release)" = "$start"
rel "$work" patch --yes --push
expect "then it releases" test "$status/$(sha "$origin" v0.3.6)" = "0/$(sha "$work" main)"
expect "... release is main" same "$origin" release "$origin" main
expect "... without the hotfix" release_in_main "$work"

echo "--- ... also when the commit is only on origin/release"
fixture stray-origin
other stray-origin-other
git -C "$other" switch -q release
commit "$other" hotfix.txt 'z innego komputera' "Hotfix pushed to release"
git -C "$other" push -q origin release
commit "$work" .github/release-notes.md '* Poprawka (0.3.6)' "Update release notes for v0.3.6"
before=$(snapshot "$work") origin0=$(origin_refs)
rel "$work" patch --yes --push
expect "a real run fails" test "$status" != 0
expect "... naming the commit from origin" has "Hotfix pushed to release"
expect "... with the way back here" has "git branch -f release $(short "$work" "$start")"
expect "... and on origin" has "(on origin too: git push --force-with-lease origin $(short "$work" "$start"):refs/heads/release)"
expect "... changes nothing" test "$(snapshot "$work")" = "$before"
expect "... and pushes nothing" test "$(origin_refs)" = "$origin0"
hint=$(sed -n 's/.*(on origin too: \(git push [^)]*\)).*/\1/p' <<<"$out")
(cd -- "$work" && eval "$hint") >/dev/null 2>&1
expect "the hint puts origin's release back" test "$(sha "$origin" release)" = "$start"
rel "$work" patch --yes --push
expect "then it releases" test "$status/$(sha "$origin" v0.3.6)" = "0/$(sha "$origin" release)"
expect "... release is main" same "$origin" release "$origin" main

echo "--- a release branch made of merges (as v0.3.6 and v0.3.7 were) is refused and put back"
fixture merges
commit "$work" .github/release-notes.md '* Poprawka (0.3.6)' "Update release notes for v0.3.6"
bump_version "$work" 0.3.6
main6=$(sha "$work" main)
git -C "$work" switch -q release
git -C "$work" merge -q --no-ff -m "Merge main into release for v0.3.6" main
git -C "$work" tag -a v0.3.6 -m "Brulion 0.3.6"
git -C "$work" switch -q main
git -C "$work" push -q origin main release v0.3.6
merge6=$(sha "$work" release)
other merges-other # a clone that keeps the merge as its local release
git -C "$other" branch -q --track release origin/release
commit "$work" fix.txt 'po 0.3.6' "Fix after v0.3.6"
before=$(snapshot "$work") origin0=$(origin_refs)
rel "$work" patch --yes --push
expect "a real run fails" test "$status" != 0
expect "... naming the merge" has "release has 1 commit that main lacks"
expect "... by its message" has "Merge main into release for v0.3.6"
expect "... and the way back to main's \"Release v0.3.6\"" has "Put release back at $(short "$work" "$main6") \"Release v0.3.6\", the last commit of main it has: git branch -f release $(short "$work" "$main6")"
expect "... on origin too" has "(on origin too: git push --force-with-lease origin $(short "$work" "$main6"):refs/heads/release)"
expect "... changes nothing" test "$(snapshot "$work")" = "$before"
expect "... and pushes nothing" test "$(origin_refs)" = "$origin0"
fix=$(sed -n 's/.*the last commit of main it has: \(git branch -f release [0-9a-f]*\).*/\1/p' <<<"$out")
hint=$(sed -n 's/.*(on origin too: \(git push [^)]*\)).*/\1/p' <<<"$out")
(cd -- "$work" && eval "$fix" && eval "$hint") >/dev/null 2>&1
expect "the way back puts release at main's commit" test "$(sha "$work" release)" = "$main6"
expect "... on origin too" test "$(sha "$origin" release)" = "$main6"
expect "... and leaves the tag v0.3.6 on the merge" test "$(sha "$origin" v0.3.6)" = "$merge6"
rel "$work" --dry-run patch
expect "unchanged notes: compared with release's commit, which has no tag" has ".github/release-notes.md has not changed since $(short "$work" "$main6") \"Release v0.3.6\""
commit "$work" .github/release-notes.md '* Druga (0.3.7)' "Update release notes for v0.3.7"
rel "$work" patch --yes --push
expect "then v0.3.7 is released" test "$status/$(sha "$origin" v0.3.7)" = "0/$(sha "$work" main)"
expect "... fast-forwarding release from main's \"Release v0.3.6\"" has "release: fast-forwards from $(short "$work" "$main6") to main's \"Release v0.3.7\""
expect "... release is main" same "$origin" release "$origin" main
expect "... without the merge" release_in_main "$origin"
expect "... and v0.3.6 still on the merge" test "$(sha "$origin" v0.3.6)" = "$merge6"
git -C "$other" pull -q --ff-only
commit "$other" .github/release-notes.md '* Trzecia (0.3.8)' "Update release notes for v0.3.8"
before=$(snapshot "$other") origin0=$(origin_refs)
rel "$other" patch --yes --push
expect "a clone whose release still is the merge refuses: diverged" has "the local release and origin/release have diverged"
expect "... with the way out" has "git branch -f release origin/release"
expect "... changing nothing" test "$(snapshot "$other")" = "$before"
expect "... and pushing nothing" test "$(origin_refs)" = "$origin0"
git -C "$other" branch -q -f release origin/release
rel "$other" patch --yes --push
expect "after git branch -f release origin/release it releases" test "$status/$(sha "$origin" v0.3.8)" = "0/$(sha "$origin" release)"

echo "--- before main took v0.3.5 over: refused until main has release's commits"
fixture legacy legacy
rel "$work" patch --yes
expect "a real run fails" test "$status" != 0
expect "... naming v0.3.5's commits" has "Release v0.3.5"
expect "... both" has "Update release notes for v0.3.5"
expect "... not with a tag error" lacks "already exists"
git -C "$work" merge -q --no-edit release
rel "$work" patch --yes
expect "after git merge release on main it releases" test "$status" = 0
expect "... 0.3.5 -> 0.3.6" has "Brulion 0.3.5 -> 0.3.6 (tag v0.3.6, from main)"
expect "... fast-forwarding release to main" test "$(sha "$work" release)" = "$(sha "$work" main)"
expect "... from v0.3.5" ancestor "$work" "$start" release

echo "--- release already at main (fast-forwarded by hand) is fine"
fixture at-main
git -C "$work" branch -q -f release main
commit "$work" .github/release-notes.md '* Poprawka (0.3.6)' "Update release notes for v0.3.6"
rel "$work" patch --yes --push
expect "the release succeeds" test "$status/$(sha "$origin" v0.3.6)" = "0/$(sha "$work" main)"
expect "... release is main" same "$origin" release "$origin" main

echo "--- release checked out in another worktree is refused"
fixture checkout
git -C "$work" worktree add -q "$tmp/checkout/release-checkout" release
before=$(snapshot "$work")
rel "$work" patch --yes
expect "a real run fails" test "$status" != 0
expect "... naming the checkout" has "release-checkout: moving the branch would leave that checkout behind"
expect "... changes nothing" test "$(snapshot "$work")" = "$before"
expect "... and leaves that checkout clean" test -z "$(git -C "$tmp/checkout/release-checkout" status --porcelain)"
git -C "$work" worktree remove "$tmp/checkout/release-checkout"
# A dry run in a checkout of release does not count that one (git lists it with the path
# resolved, also when it was added through a symbolic link, like --show-toplevel).
ln -s -- "$tmp/checkout" "$tmp/checkout-link"
git -C "$work" worktree add -q "$tmp/checkout-link/via-link" release
rel "$tmp/checkout-link/via-link" --dry-run patch
expect "a dry run in the release checkout itself does not count it" lacks "is checked out in"
git -C "$work" worktree remove "$tmp/checkout-link/via-link"
rel "$work" patch --yes
expect "once it is gone it releases" test "$status/$(sha "$work" v0.3.6)" = "0/$(sha "$work" release)"
expect "... release is main" same "$work" release "$work" main

echo "--- pre-releases, and versions that are already recorded"
fixture rc
commit "$work" .github/release-notes.md '* Wydanie testowe' "Update release notes for v0.4.0-rc.1"
rel "$work" 0.4.0-rc.1 --yes --push
expect "rc.1 succeeds" test "$status" = 0
expect "... with \"Release v0.4.0-rc.1\" on main" test "$(subject "$work" main)" = "Release v0.4.0-rc.1"
expect "... which records 0.4.0" test "$(version_on "$work" main)/$(nix_version_on "$work" main)" = 0.4.0/0.4.0
expect "... and its tag on that commit" same "$origin" v0.4.0-rc.1 "$work" main
expect "... which release is too" same "$work" release "$work" main
commit "$work" fix.txt 'poprawka do rc' "Fix for rc.2"
main0=$(sha "$work" main) release0=$(sha "$work" release)
rel "$work" 0.4.0-rc.2 --yes --push
expect "rc.2 makes no version commit" has "CMakeLists.txt already has 0.4.0: no version commit"
expect "... main stays" test "$(sha "$work" main)" = "$main0"
expect "... release fast-forwards to main's fix" test "$(sha "$work" release)" = "$main0"
expect "... as it says" has "release: fast-forwards from $(short "$work" "$release0") to main's $(short "$work" "$main0") \"Fix for rc.2\" (1 commit of main) and tags it v0.4.0-rc.2"
expect "... tagged rc.2 there and pushed" same "$origin" v0.4.0-rc.2 "$work" main
expect "... with release" same "$origin" release "$work" release
before=$(snapshot "$work")
rel "$work" patch --yes
expect "patch while 0.4.0 has only pre-releases is refused" has "0.4.0 has pre-releases (v0.4.0-rc.1 v0.4.0-rc.2) but no release: patch would skip it for 0.4.1"
expect "... naming the way out" has "release it with: scripts/release.sh 0.4.0"
expect "... changing nothing" test "$(snapshot "$work")" = "$before"
# Release does not move now: a checkout of it elsewhere is no obstacle.
git -C "$work" worktree add -q "$tmp/rc/release-checkout" release
release0=$(sha "$work" release)
rel "$work" 0.4.0 --yes --push
expect "the final release succeeds (release checked out elsewhere, but not moving)" test "$status" = 0
expect "... says release is there already" has "release: is at main's $(short "$work" "$main0") \"Fix for rc.2\" already: tags it v0.4.0"
expect "... moves nothing" test "$(sha "$work" release)" = "$release0"
expect "... and adds no commit on main" test "$(sha "$work" main)" = "$main0"
expect "... tags rc.2's commit" same "$work" v0.4.0 "$work" v0.4.0-rc.2
expect "... on origin too" same "$origin" v0.4.0 "$origin" release
expect "... which is main" same "$origin" v0.4.0 "$origin" main
git -C "$work" worktree remove "$tmp/rc/release-checkout"
before=$(snapshot "$work")
rel "$work" 0.4.0-rc.3 --yes
expect "a pre-release after the final one is refused" test "$status" != 0
expect "... as too late" has "v0.4.0 is already released"
expect "... changing nothing" test "$(snapshot "$work")" = "$before"
rel "$work" --dry-run patch
expect "once 0.4.0 is released, patch is fine" lacks "pre-releases"
rel "$work" v0.4.1-rc.1 --yes
expect "a leading v is accepted" same "$work" v0.4.1-rc.1 "$work" release
expect "... and the pre-release bumps to 0.4.1" test "$(subject "$work" main)/$(version_on "$work" main)" = "Release v0.4.1-rc.1/0.4.1"
expect "... on main's head" same "$work" v0.4.1-rc.1 "$work" main
expect "release has no merge since v0.3.5" no_merge_since "$work" "$start"

echo "--- versions and tags that cannot be released"
fixture versions
before=$(snapshot "$work")
rel "$work" 0.3.4 --yes
expect "an older version is refused" has "0.3.4 is older than the current version 0.3.5"
rel "$work" 0.1000.0 --yes
expect "minor 1000 is refused" has "minor and patch must be below 1000"
rel "$work" 0.3.5 --yes
expect "an existing tag is refused" has "tag v0.3.5 already exists"
rel "$work" 1.0 --yes
expect "a malformed version is refused" has "not a version X.Y.Z[-pre]: 1.0"
other versions-other
git -C "$other" tag -a v0.3.6 -m "Elsewhere"
git -C "$other" push -q origin v0.3.6
rel "$work" patch --yes --push
expect "a tag only on origin is refused" has "tag v0.3.6 already exists on origin"
expect "none of these changes anything" test "$(snapshot "$work")" = "$before"

echo "--- a failed step undoes the whole run"
fixture fail
other fail-other
commit "$work" .github/release-notes.md '* Poprawka (0.3.6)' "Update release notes for v0.3.6"
before=$(snapshot "$work") origin0=$(origin_refs)
hook "$work" pre-commit 'exit 1'
rel "$work" patch --yes --push
expect "a refused version commit fails the run" test "$status" != 0
expect "... saying where" has 'release.sh failed while committing "Release v0.3.6" on main.'
expect "... and that main is back" has "Undone: put main back at"
expect "... leaving everything as before" test "$(snapshot "$work")" = "$before"
expect "... on a clean main" on_main_clean "$work"
expect "... pushing nothing" test "$(origin_refs)" = "$origin0"
rm -- "$work/.git/hooks/pre-commit"
# $1 is the hook's own argument (the transaction's state).
# shellcheck disable=SC2016
hook "$work" reference-transaction '[[ $1 != prepared ]] || ! grep " refs/tags/v0.3.6$" >/dev/null'
rel "$work" patch --yes --push
expect "a tag that cannot be made fails the run after release moved" test "$status" != 0
expect "... saying where" has "release.sh failed while tagging"
expect "... putting release back" has "Undone: put release back at $(short "$work" "$start")."
expect "... and the version commit" has "Undone: put main back at"
expect "... leaving everything as before" test "$(snapshot "$work")" = "$before"
expect "... on a clean main" on_main_clean "$work"
expect "... pushing nothing" test "$(origin_refs)" = "$origin0"
rm -- "$work/.git/hooks/reference-transaction"
# Origin refuses the push (a server-side hook): the atomic push changes nothing there.
hook "$origin" pre-receive 'cat >/dev/null; echo "odrzucone przez serwer" >&2; exit 1'
rel "$work" patch --yes --push
expect "a push refused by origin fails the run" test "$status" != 0
expect "... saying origin is unchanged" has "release.sh failed while pushing to origin (origin is unchanged: the push is atomic)."
expect "... with origin's reason" has "odrzucone przez serwer"
expect "... undoing the tag" has "Undone: deleted the tag v0.3.6."
expect "... putting release back" has "Undone: put release back at $(short "$work" "$start")."
expect "... and main" has "Undone: put main back at $(short "$work" main)"
expect "... leaving everything as before" test "$(snapshot "$work")" = "$before"
expect "... on a clean main" on_main_clean "$work"
expect "... and origin as it was" test "$(origin_refs)" = "$origin0"
rm -- "$origin/hooks/pre-receive"
# The tag appears on origin after the checks: pushed from elsewhere just before this push.
hook "$work" pre-push "cat >/dev/null
unset \$(git rev-parse --local-env-vars)
git -C '$other' tag -a v0.3.6 -m Elsewhere && git -C '$other' push -q origin refs/tags/v0.3.6"
rel "$work" patch --yes --push
expect "a rejected push fails the run" test "$status" != 0
expect "... saying origin is unchanged" has "release.sh failed while pushing to origin (origin is unchanged: the push is atomic)."
expect "... undoing the tag" has "Undone: deleted the tag v0.3.6."
expect "... release's move" has "Undone: put release back at"
expect "... and the version commit" has "Undone: put main back at"
expect "... leaving everything as before" test "$(snapshot "$work")" = "$before"
expect "... on a clean main" on_main_clean "$work"
expect "origin's main did not move" same "$origin" main "$work" origin/main
expect "... nor its release (atomic push)" test "$(sha "$origin" release)" = "$start"
expect "... it has only the tag from elsewhere" same "$origin" v0.3.6 "$other" main
rm -- "$work/.git/hooks/pre-push"
rel "$work" patch --yes --push
expect "run again it refuses the tag on origin" has "tag v0.3.6 already exists on origin"
expect "... before changing anything" test "$(snapshot "$work")" = "$before"
git -C "$origin" tag -d v0.3.6 >/dev/null
rel "$work" patch --yes --push
expect "once that tag is gone it releases" test "$status/$(sha "$origin" v0.3.6)" = "0/$(sha "$work" release)"
expect "... release is main" same "$origin" release "$origin" main

echo "--- a missing, a stale and a diverged local release branch"
fixture stale
other stale-other
commit "$other" .github/release-notes.md '* Z innego komputera (0.3.6)' "Update release notes for v0.3.6"
rel "$other" patch --yes --push
expect "a clone without a local release releases" test "$status" = 0
expect "... creating it from origin/release" has "release: the local branch release is created from origin/release"
expect "... tracking it" test "$(git -C "$other" rev-parse --abbrev-ref 'release@{upstream}')" = origin/release
expect "... and fast-forwarding it to main" same "$other" release "$other" main
expect "... from origin's release" has "release: fast-forwards from $(short "$other" "$start") to main's \"Release v0.3.6\""
expect "... pushed" same "$origin" release "$other" release
git -C "$work" pull -q --ff-only
commit "$work" .github/release-notes.md '* Następna (0.3.7)' "Update release notes for v0.3.7"
expect "here the local release is behind" test "$(sha "$work" release)" = "$start"
rel "$work" patch --yes --push
expect "the next release succeeds" test "$status" = 0
expect "... fast-forwarding release to origin/release first" has "release: the local release is fast-forwarded to origin/release"
expect "... then on to main" has "release: fast-forwards from $(short "$work" "$(sha "$other" release)") to main's \"Release v0.3.7\""
expect "... release is main" same "$work" release "$work" main
expect "... and is pushed" same "$origin" release "$work" release
# Diverged: a commit of its own made here on release but never pushed, while origin's release
# moved on.
git -C "$work" update-ref refs/heads/release "$(git -C "$work" commit-tree -p release -m "Unpushed commit" "release^{tree}")"
git -C "$other" pull -q --ff-only
commit "$other" .github/release-notes.md '* Jeszcze (0.3.8)' "Update release notes for v0.3.8"
rel "$other" patch --yes --push
git -C "$work" pull -q --ff-only
before=$(snapshot "$work") origin0=$(origin_refs)
rel "$work" patch --yes --push
expect "a diverged release is refused" has "the local release and origin/release have diverged"
expect "... with the way out" has "when the local one is wrong: git branch -f release origin/release"
expect "... changing nothing" test "$(snapshot "$work")" = "$before"
expect "... and pushing nothing" test "$(origin_refs)" = "$origin0"
git -C "$work" branch -q -f release origin/release
commit "$work" .github/release-notes.md '* Ostatnia (0.3.9)' "Update release notes for v0.3.9"
rel "$work" patch --yes --push
expect "after git branch -f release origin/release it releases" test "$status/$(sha "$origin" v0.3.9)" = "0/$(sha "$origin" release)"
expect "... release is main" same "$origin" release "$origin" main

echo "--- unchanged release notes are pointed out: those of release's commit, the last release"
fixture notes
rel "$work" --dry-run patch
expect "notes unchanged since v0.3.5" has ".github/release-notes.md has not changed since v0.3.5"
commit "$work" .github/release-notes.md '* Nowość (0.3.6)' "Update release notes for v0.3.6"
rel "$work" --dry-run patch
expect "changed notes: no warning" lacks "has not changed"
rel "$work" patch --yes --push
commit "$work" fix.txt 'kolejna' "Next fix on main"
rel "$work" --dry-run patch
expect "after v0.3.6: unchanged since v0.3.6 (release's commit, on main too)" has ".github/release-notes.md has not changed since v0.3.6"

echo "--- with a news section only its news count, not the summary line under it"
fixture news
news_notes() { # <clone> <news line> <summary line>: rewrites the notes with a news section
  printf '%s\n' '**Brulion {{VERSION}}** — notatki' '' '**Nowości w {{VERSION}}**' '' "$2" '' "$3" '' \
    '**Najważniejsze**' '' '* Konspekty i dziennik' >"$1/.github/release-notes.md"
  git -C "$1" commit -q -am "Update release notes"
}
news_notes "$work" '* Zegar (0.3.6)' 'Z wersji 0.3.5: obrazy.'
rel "$work" patch --yes --push
expect "a release with a news section" test "$status" = 0
news_notes "$work" '* Zegar (0.3.6)' 'Z wersji 0.3.6: zegar. Z wersji 0.3.5: obrazy.'
rel "$work" --dry-run patch
expect "only the summary changed: still unchanged news" has ".github/release-notes.md has not changed since v0.3.6"
news_notes "$work" '* Statusy zadań (0.3.7)' 'Z wersji 0.3.6: zegar. Z wersji 0.3.5: obrazy.'
rel "$work" --dry-run patch
expect "new news: no warning" lacks "has not changed"

echo "--- the documentation's list of changes needs a section for the release"
fixture changelog
mkdir -p "$work/src/app/shell/guide"
commit "$work" 'src/app/shell/guide/Lista zmian.md' '## 0.3.5 — 21 października 2026' "List the changes of 0.3.5"
rel "$work" --dry-run patch
expect "no section for 0.3.6: pointed out" has 'Lista zmian.md has no section "## 0.3.6"'
commit "$work" 'src/app/shell/guide/Lista zmian.md' '## 0.3.6 — 22 października 2026' "List the changes of 0.3.6"
rel "$work" --dry-run patch
expect "a section for 0.3.6: no warning" lacks 'has no section'
rel "$work" --dry-run 0.4.0-rc.1
expect "a pre-release needs no section" lacks 'has no section'

echo "--- a dirty working tree, an unreachable origin"
fixture misc
printf '%s\n' 'niezapisane' >>"$work/fix.txt"
rel "$work" patch --yes
expect "uncommitted changes are refused" has "error: the working tree has uncommitted changes"
rel "$work" --dry-run patch
expect "... a dry run on main warns about them" succeeds_with "A real run refuses"
git -C "$work" checkout -q -- fix.txt
git -C "$work" remote set-url origin "$tmp/misc/nowhere.git"
before=$(snapshot "$work")
rel "$work" patch --yes --push
expect "--push without a reachable origin is refused" has "--push: origin is not reachable"
expect "... before changing anything" test "$(snapshot "$work")" = "$before"
rel "$work" patch --yes
expect "without --push it releases locally" test "$status/$(sha "$work" v0.3.6)" = "0/$(sha "$work" release)"
expect "... after a warning" has "warning: origin is not reachable"
git -C "$work" remote set-url origin "$origin"
commit "$work" .github/release-notes.md '* Następna (0.3.7)' "Update release notes for v0.3.7"
rel "$work" patch --yes --push
expect "the next release with --push succeeds" test "$status/$(sha "$origin" v0.3.7)" = "0/$(sha "$work" release)"
expect "... sending release's unpushed commits along" ancestor "$origin" "$(sha "$work" v0.3.6)" release
expect "... release is main" same "$origin" release "$origin" main
expect "... but says v0.3.6's tag stays here" has "they carry the tags v0.3.6, which this run does not push (git push origin v0.3.6)"

echo
if ((failed)); then
  echo "Totals: $failed failed"
  exit 1
fi
echo "Totals: all passed"
