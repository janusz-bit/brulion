#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Releases Brulion from main: commits the new version on main ("Release v<version>"), merges
# main into the release branch ("Merge main into release for v<version>", always a merge
# commit) and puts the annotated tag v<version> on that merge. Pushing the tag starts
# .github/workflows/release.yml (Android APK, Linux AppImages, web zip, source tarball ->
# GitHub release).
#
# Every commit goes on main: the development, the release notes (.github/release-notes.md) and
# the version. The release branch holds nothing but merges of main, one per release, each with
# its tag. Run it on main with a clean working tree. It first fetches main and release from
# origin and refuses when main lacks commits of origin/main, when the local release and
# origin/release have diverged, when release has a commit that main lacks and that is not a
# merge (git rev-list --no-merges main..release must be empty) or a merge with changes of its
# own, when release was fast-forwarded to main (main's commits on its first-parent line), or when
# release is checked out in another worktree. The merge is made without checking release out:
# the working tree stays on main. When a step fails, everything the run did is undone (and
# nothing is pushed).
#
# Usage: scripts/release.sh <X.Y.Z[-pre] | major | minor | patch> [--push] [--yes] [--dry-run]
#   X.Y.Z[-pre]  the new version; a suffix (0.2.0-rc.1) only marks the tag as a pre-release
#                (CMake and the Android versionName keep 0.2.0)
#   major|minor|patch  bump that part of the current version (refused while the current version
#              has pre-releases but no release: give the version itself)
#   --push     then push main, release and the tag, atomically:
#              git push --atomic origin main release refs/tags/v<version>
#   --yes      do not ask for confirmation
#   --dry-run  show what a real run would do and what it would refuse; change nothing (no fetch:
#              origin is only asked for its refs). From another branch it shows what a real run
#              on main would do.
#
# Updated files: CMakeLists.txt (project VERSION — the app, the Android versionName and
# versionCode = major*1000000 + minor*1000 + patch, so minor and patch stay below 1000) and
# nix/package.nix (`version = "...";`). Releasing the version that is already recorded (a
# further pre-release, or the final release after its pre-releases) makes no version commit: it
# merges main when main has anything release lacks, and otherwise tags release's last merge.
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}
warn() { printf 'warning: %s\n' "$*" >&2; }
usage() { awk 'NR > 2 && /^#/ { sub(/^# ?/, ""); print; next } NR > 2 { exit }' "${BASH_SOURCE[0]}"; }

bump='' push=0 yes=0 dry=0
for arg in "$@"; do
  case $arg in
    --push) push=1 ;;
    --yes | -y) yes=1 ;;
    --dry-run | -n) dry=1 ;;
    -h | --help)
      usage
      exit 0
      ;;
    -*) die "unknown option $arg (see --help)" ;;
    *)
      [[ -z $bump ]] || die "more than one version given"
      bump=$arg
      ;;
  esac
done
[[ -n $bump ]] || {
  usage >&2
  exit 2
}

git rev-parse --is-inside-work-tree >/dev/null 2>&1 || die "not a git repository"

# refuse <message>: a real run stops here; a dry run reports it and goes on.
refused=0
refuse() {
  ((dry)) || die "$1"
  warn "$1"
  refused=1
}
known() { git rev-parse -q --verify "$1^{commit}" || true; }
short() { git rev-parse --short "$1"; }
commits() { # <count>: "1 commit", "2 commits"
  if (($1 == 1)); then echo "1 commit"; else echo "$1 commits"; fi
}

# The branch first, so that nothing below hides this message. A dry run elsewhere shows what a
# real run on main would do: it reads main (the local one, else origin's), not this checkout.
branch=$(git symbolic-ref --short -q HEAD || echo "(detached)")
main_ref=refs/heads/main from=main
if [[ $branch != main ]]; then
  msg="releases are made on main, not '$branch' (git switch main)"
  ((dry)) || die "$msg"
  warn "$msg; this is what a real run on main would do:"
  if [[ -z $(known "$main_ref") ]]; then main_ref=refs/remotes/origin/main from=origin/main; fi
fi
main_head=$(known "$main_ref")
[[ -n $main_head ]] || die "there is no branch main here"

if [[ $branch == main ]] && { ! git diff --quiet || ! git diff --cached --quiet; }; then
  refuse "the working tree has uncommitted changes (commit or stash them first)"
fi

cmake_file=CMakeLists.txt
nix_file=nix/package.nix
cmake_text=$(git show "$main_head:$cmake_file") || die "cannot read $cmake_file"
nix_text=$(git show "$main_head:$nix_file") || die "cannot read $nix_file"
current=$(sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+)[[:space:]]*$/\1/p' <<<"$cmake_text" | head -n 1)
[[ $current =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]] || die "no project VERSION X.Y.Z found in $cmake_file"
major=${BASH_REMATCH[1]} minor=${BASH_REMATCH[2]} patch=${BASH_REMATCH[3]}
nix_current=$(sed -nE 's/^[[:space:]]*version = "([^"]+)";.*/\1/p' <<<"$nix_text" | head -n 1)
[[ -n $nix_current ]] || die "no version = \"...\"; found in $nix_file"
[[ $nix_current == "$current" ]] ||
  warn "$nix_file has version $nix_current, $cmake_file $current; both get the new version"

case $bump in
  major) new=$((major + 1)).0.0 ;;
  minor) new=$major.$((minor + 1)).0 ;;
  patch) new=$major.$minor.$((patch + 1)) ;;
  *) new=${bump#v} ;;
esac
[[ $new =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z][0-9A-Za-z.-]*)?$ ]] ||
  die "not a version X.Y.Z[-pre]: $new"
release=${BASH_REMATCH[1]}.${BASH_REMATCH[2]}.${BASH_REMATCH[3]}
((BASH_REMATCH[2] < 1000 && BASH_REMATCH[3] < 1000)) ||
  die "minor and patch must be below 1000 (Android versionCode = major*1000000 + minor*1000 + patch)"
newest=$(printf '%s\n%s\n' "$current" "$release" | sort -V | tail -n 1)
[[ $newest == "$release" ]] || die "$release is older than the current version $current"
tag=v$new final=v$release

# One round trip to origin for its main, its release and the tags; a real run then fetches the
# branches, so that the checks below see every commit on origin.
remote_refs='' have_origin=0 reached=0
if git remote get-url origin >/dev/null 2>&1; then
  have_origin=1
  if remote_refs=$(git ls-remote origin refs/heads/main refs/heads/release "refs/tags/$tag" "refs/tags/$final" "refs/tags/v$current" 2>/dev/null); then
    reached=1
  else
    warn "origin is not reachable: comparing with the last fetched origin/main and origin/release"
  fi
fi
((!push || have_origin)) || die "--push: there is no remote origin"
((!push || reached)) || refuse "--push: origin is not reachable (release without --push and push later)"
remote_commit() { awk -v ref="$1" '$2 == ref { print $1; exit }' <<<"$remote_refs"; }
if ((reached && !dry)); then
  specs=()
  for b in main release; do
    if [[ -n $(remote_commit "refs/heads/$b") ]]; then specs+=("+refs/heads/$b:refs/remotes/origin/$b"); fi
  done
  if ((${#specs[@]})); then
    git fetch -q --no-tags origin "${specs[@]}" || die "could not fetch main and release from origin"
  fi
fi
origin_head() { # <branch>: origin's commit of it, as far as this run knows
  if ((reached)); then remote_commit "refs/heads/$1"; else known "refs/remotes/origin/$1"; fi
}
origin_main=$(origin_head main) origin_release=$(origin_head release)

# Main must have everything origin/main has: else the release lacks those commits (and the push
# of main would be rejected after the release was made).
if [[ -n $origin_main && $origin_main != "$main_head" ]]; then
  if ! git cat-file -e "$origin_main^{commit}" 2>/dev/null; then
    refuse "main lacks commits of origin/main that are not fetched yet (git pull --ff-only)"
  elif ! git merge-base --is-ancestor "$origin_main" "$main_head"; then
    refuse "main lacks $(commits "$(git rev-list --count "$main_head..$origin_main")") of origin/main (git pull --ff-only, or git pull --rebase when main has commits of its own)"
  fi
fi

# The release branch the merge goes onto: the local one, created from origin/release when there
# is none, fast-forwarded to origin/release when that moved on.
local_release=$(known refs/heads/release)
if [[ -n $origin_release ]] && ! git cat-file -e "$origin_release^{commit}" 2>/dev/null; then
  warn "origin/release has commits that are not fetched yet: a real run fetches and checks them too"
  origin_release=$(known refs/remotes/origin/release)
fi
release_head=$local_release release_note='' create_release=0 ff_release=0
if [[ -n $origin_release && $origin_release != "$local_release" ]]; then
  if [[ -z $local_release ]]; then
    release_head=$origin_release create_release=1
    release_note="the local branch release is created from origin/release ($(short "$origin_release"))"
  elif git merge-base --is-ancestor "$local_release" "$origin_release"; then
    release_head=$origin_release ff_release=1
    release_note="the local release is fast-forwarded to origin/release ($(short "$origin_release"))"
  elif git merge-base --is-ancestor "$origin_release" "$local_release"; then
    warn "the local release is $(commits "$(git rev-list --count "$origin_release..$local_release")") ahead of origin/release (not pushed yet): pushing sends them along"
    # An earlier release made without --push: its tag is not part of this run's push.
    unpushed=$(git tag -l 'v[0-9]*' --merged "$local_release" --no-merged "$origin_release" | paste -sd ' ' -)
    [[ -z $unpushed ]] || warn "they carry the tags $unpushed, which this run does not push (git push origin $unpushed)"
  else
    refuse "the local release and origin/release have diverged (see git log --graph release origin/release; when the local one is wrong: git branch -f release origin/release)"
  fi
fi
[[ -n $release_head ]] ||
  die "there is no release branch, neither here nor on origin (create it once at the last release: git branch release v<version> && git push origin release)"

# The rule: release holds only merges of main, so that every release is exactly a state of main. A
# commit of release's own would ship without ever being on main (nor in what GitHub Pages and the
# CI build from it), and later merges could conflict with it.
rule="The release branch holds only merges of main: every commit goes on main."
stray=$(git rev-list --count --no-merges "$main_head..$release_head")
if ((stray)); then
  more=''
  ((stray <= 10)) || more=$'\n'"  … and $((stray - 10)) more"
  refuse "release has commits that main lacks and that are not merges (git rev-list --no-merges main..release must be empty):
$(git log --no-merges --format='  %h %s' -n 10 "$main_head..$release_head")$more
$rule Bring them to main first (on main: git merge release), then release."
elif ! base=$(git merge-base "$main_head" "$release_head"); then
  refuse "main and release have no commit in common"
elif ! git diff --quiet "$base" "$release_head"; then
  refuse "release has changes of its own: it differs from $(short "$base"), the commit of main it merged last, in
$(git diff --name-only "$base" "$release_head" | head -n 10 | sed 's/^/  /')
$rule Bring the changes to main first (on main: git merge release), then release."
elif last_release=$(git describe --tags --abbrev=0 --first-parent --match 'v[0-9]*' "$release_head" 2>/dev/null) &&
  ff_commits=$(git rev-list --count --first-parent --no-merges "$last_release..$release_head") && ((ff_commits)); then
  # Main's own commits on release's line: release was fast-forwarded to main (`git merge main`
  # on release while it had nothing main lacks, or a push of main to release). Main has them,
  # so the checks above pass, but release would no longer hold only merges.
  refuse "release was fast-forwarded to main: since $last_release its first-parent line has $(commits "$ff_commits") of main instead of merges
$rule Put release back at $last_release first (git branch -f release $last_release; on origin too when it is there: git push --force-with-lease origin $last_release^{commit}:refs/heads/release), then release."
fi

# A checkout of release elsewhere (another worktree) would be left behind when the branch moves:
# its files and index would show the reverse of the merge as changes of its own. This checkout
# does not count: a real run has main checked out here (a dry run may be on release).
release_checkout=$(git worktree list --porcelain | awk -v here="$(git rev-parse --show-toplevel)" '
  /^worktree / { path = substr($0, 10) }
  $0 == "branch refs/heads/release" && path != here && !found { print path; found = 1 }')

if git rev-parse -q --verify "refs/tags/$tag" >/dev/null; then refuse "tag $tag already exists"; fi
[[ -z $(remote_commit "refs/tags/$tag") ]] || refuse "tag $tag already exists on origin"
if [[ $new != "$release" ]] &&
  { git rev-parse -q --verify "refs/tags/$final" >/dev/null || [[ -n $(remote_commit "refs/tags/$final") ]]; }; then
  refuse "$final is already released: the pre-release $tag would come after it"
fi
# After 0.4.0-rc.N the files record 0.4.0, still unreleased: `patch` would skip it for 0.4.1.
case $bump in
  major | minor | patch)
    pre_tags=$(git tag -l "v$current-*" | paste -sd ' ' -)
    if [[ -n $pre_tags ]] && ! git rev-parse -q --verify "refs/tags/v$current" >/dev/null &&
      [[ -z $(remote_commit "refs/tags/v$current") ]]; then
      refuse "$current has pre-releases ($pre_tags) but no release: $bump would skip it for $new (release it with: scripts/release.sh $current; or give $new itself)"
    fi
    ;;
esac

# The notes are published as the release's description: unchanged ones repeat the last release's.
# Release tags sit on release's merges, which main does not contain: look for them from release.
# Only the news count: the "Nowości w …" section without its "Z wersji …" summary line, so that
# editing the summary alone does not hide unchanged news (the whole file when it has no section).
notes=.github/release-notes.md
news_of() {
  git show "$1:$notes" 2>/dev/null | awk '
    /^\*\*Nowości w / { on = 1; seen = 1; next }
    on && /^\*\*/ { on = 0 }
    on && !/^Z wersji / { news = news $0 "\n" }
    { all = all $0 "\n" }
    END { printf "%s", seen ? news : all }'
}
if last=$(git describe --tags --abbrev=0 "$release_head" 2>/dev/null) &&
  [[ $(news_of "$last") == "$(news_of "$main_head")" ]]; then
  warn "$notes has not changed since $last (its news; commit \"Update release notes for $tag\" on main first)"
fi
# The documentation notebook's list of changes has a section "## X.Y.Z — <date>" per release (a
# pre-release needs none).
changelog='src/app/shell/guide/Lista zmian.md'
if [[ $new == "$release" ]] && git cat-file -e "$main_head:$changelog" 2>/dev/null &&
  ! git show "$main_head:$changelog" | grep -qE "^## ${release//./\\.}( |$)"; then
  warn "$changelog has no section \"## $release\" (add the changes of $tag to it on main first)"
fi

files=()
if [[ $release != "$current" || $nix_current != "$release" ]]; then
  files=("$cmake_file" "$nix_file")
fi
# Without a version commit main may bring nothing new: then the release is release's last commit
# (e.g. the final release of what the last pre-release merged), which only gets the tag.
merge=1
if ((${#files[@]} == 0)) && git merge-base --is-ancestor "$main_head" "$release_head"; then merge=0; fi
if [[ -n $release_checkout ]] && ((merge || ff_release)); then
  refuse "release is checked out in $release_checkout: moving the branch would leave that checkout behind (switch it to another branch, or remove it: git worktree remove)"
fi
message="Merge main into release for $tag"
push_cmd=(git push --atomic origin main release "refs/tags/$tag")

echo "Brulion $current -> $new (tag $tag, from $from)"
if ((${#files[@]})); then
  echo "  main:    updates ${files[*]} to $release and commits \"Release $tag\""
else
  echo "  main:    $cmake_file already has $release: no version commit"
fi
[[ -z $release_note ]] || echo "  release: $release_note"
if ((merge)); then
  echo "  release: merges main onto $(short "$release_head") (\"$message\") and tags the merge $tag"
else
  echo "  release: has everything on main already: tags its last commit $(git log -1 --format='%h "%s"' "$release_head") $tag"
fi
if ((push)); then echo "  push:    ${push_cmd[*]}"; fi
if ((dry)); then
  if ((refused)); then echo "A real run refuses to release (see the warnings above)."; fi
  exit 0
fi

if ((!yes)) && [[ -t 0 ]]; then
  # Ctrl+D (end of input) answers no, instead of ending the script without a word under set -e.
  read -r -p "Release $tag? [y/N] " answer || answer=''
  [[ $answer == [yY]* ]] || die "aborted"
fi

# From here on refs change. When a step fails, the trap undoes everything this run did (the tag,
# the release branch, the version commit) and says so; HEAD stays on main throughout.
orig_main=$main_head step='' finished=0 bumped=0 release_moved=0 tagged=0
undo() {
  local status=$?
  ((finished)) && return "$status"
  set +e
  local undone=()
  if ((tagged)) && git tag -d "$tag" >/dev/null; then undone+=("deleted the tag $tag"); fi
  if ((release_moved)); then
    if [[ -n $local_release ]]; then
      git update-ref -m "release.sh: undo" refs/heads/release "$local_release" &&
        undone+=("put release back at $(short "$local_release")")
    else
      git branch -q -D release && undone+=("deleted the local branch release again")
    fi
  fi
  if ((bumped)); then
    rm -f -- "$cmake_file.tmp" "$nix_file.tmp"
    git reset -q --soft "$orig_main" && git checkout -q "$orig_main" -- "$cmake_file" "$nix_file" &&
      undone+=("put main back at $(short "$orig_main") (without \"Release $tag\")")
  fi
  printf '%s\n' "release.sh failed while $step." >&2
  if ((${#undone[@]})); then printf 'Undone: %s.\n' "${undone[@]}" >&2; fi
  echo "The repository is as it was before; fix the cause and run release.sh again." >&2
  exit "$((status ? status : 1))"
}
trap undo EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

if ((create_release)); then
  step="creating the local branch release"
  release_moved=1
  git branch -q --track release refs/remotes/origin/release
elif ((ff_release)); then
  step="fast-forwarding release to origin/release"
  release_moved=1
  git update-ref -m "release.sh: fast-forward to origin/release" refs/heads/release "$release_head" "$local_release"
fi

if ((${#files[@]})); then
  step="committing \"Release $tag\" on main"
  bumped=1
  # CMakeLists.txt: the first `VERSION X.Y.Z` line (the one inside project()).
  awk -v new="$release" '
    !done && /^[[:space:]]*VERSION[[:space:]]+[0-9]+\.[0-9]+\.[0-9]+[[:space:]]*$/ {
      sub(/[0-9]+\.[0-9]+\.[0-9]+/, new); done = 1
    }
    { print }
    END { if (!done) exit 3 }
  ' "$cmake_file" >"$cmake_file.tmp" || die "could not update $cmake_file"
  mv -- "$cmake_file.tmp" "$cmake_file"
  awk -v new="$release" '
    !done && /^[[:space:]]*version = "[^"]*";/ { sub(/"[^"]*"/, "\"" new "\""); done = 1 }
    { print }
    END { if (!done) exit 3 }
  ' "$nix_file" >"$nix_file.tmp" || die "could not update $nix_file"
  mv -- "$nix_file.tmp" "$nix_file"
  git --no-pager diff --stat -- "${files[@]}"
  git commit -q -m "Release $tag" -- "${files[@]}"
fi

target=$release_head
if ((merge)); then
  # `git merge --no-ff main` on release without checking it out (no file on disk changes, so
  # nothing gets rebuilt): release carries nothing main lacks (checked above), so the merge's
  # tree is main's.
  step="merging main into release"
  main_new=$(git rev-parse HEAD)
  target=$(git commit-tree -p "$release_head" -p "$main_new" -m "$message" "$main_new^{tree}")
  release_moved=1
  git update-ref -m "release.sh: $message" refs/heads/release "$target" "$release_head"
fi

step="tagging $(short "$target") $tag"
git tag -a "$tag" -m "Brulion $new" "$target"
tagged=1

if ((push)); then
  step="pushing to origin (origin is unchanged: the push is atomic)"
  "${push_cmd[@]}"
fi
finished=1

echo "Released $tag: release $(git log -1 --format='%h "%s"' "$target"), main $(git log -1 --format='%h "%s"' HEAD)."
if ((push)); then
  echo "Pushed main, release and $tag to origin: release.yml builds and publishes the release."
else
  undo_cmd="git tag -d $tag"
  if ((merge)); then undo_cmd+=" && git branch -f release $(short "$release_head")"; fi
  if ((${#files[@]})); then undo_cmd+=" && git reset --keep $(short "$orig_main")"; fi
  echo "Publish it with:  ${push_cmd[*]}"
  echo "Undo it (before pushing) with:  $undo_cmd"
fi
