#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Sets the Brulion version where it is recorded, commits, and creates the annotated tag
# v<version>. Pushing the tag starts .github/workflows/release.yml (Android APK, Linux
# AppImages, web zip, source tarball -> GitHub release).
#
# Run it on the release branch: main holds the development, release holds the releases —
# switch to release and merge main into it first (release.sh refuses any other branch, and a
# release branch that lacks commits of main, origin/main or origin/release). Afterwards merge
# release back into main, so that main has the new version and notes too (it prints how).
#
# Usage: scripts/release.sh <X.Y.Z[-pre] | major | minor | patch> [--push] [--yes] [--dry-run]
#                           [--allow-stale]
#   X.Y.Z[-pre]  the new version; a suffix (0.2.0-rc.1) only marks the tag as a pre-release
#                (CMake and the Android versionName keep 0.2.0)
#   major|minor|patch  bump that part of the current version
#   --push     push the release branch and the tag to origin right away (atomically)
#   --yes      do not ask for confirmation
#   --dry-run  show what a real run would do, change nothing; from another branch it shows what a
#              real run on the release branch would do (the versions come from that branch)
#   --allow-stale  release even though main or origin/main has commits that the release branch
#              lacks (never without origin/release's: origin would reject the push)
#
# Updated files: CMakeLists.txt (project VERSION — the app, the Android versionName and
# versionCode = major*1000000 + minor*1000 + patch, so minor and patch stay below 1000) and
# nix/package.nix (`version = "...";`). Releasing the version that is already recorded (e.g. the
# first release, or a further pre-release) only creates the tag; the branch is still pushed with
# it, so the tag never points at commits that no branch on origin has.
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}
usage() { awk 'NR > 2 && /^#/ { sub(/^# ?/, ""); print; next } NR > 2 { exit }' "${BASH_SOURCE[0]}"; }

bump='' push=0 yes=0 dry=0 allow_stale=0
for arg in "$@"; do
  case $arg in
    --push) push=1 ;;
    --yes | -y) yes=1 ;;
    --dry-run | -n) dry=1 ;;
    --allow-stale) allow_stale=1 ;;
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

# The branch first, so that nothing below hides this message. A real run only releases from
# release; a dry run elsewhere shows what a real run on release would do, so it reads the
# versions from the release branch (the local one, else origin's) instead of this checkout.
branch=$(git symbolic-ref --short -q HEAD || echo "(detached)")
from=$branch src_ref=''
if [[ $branch != release ]]; then
  msg="releases go on the 'release' branch, not '$branch' (git switch release && git merge main)"
  ((dry)) || die "$msg"
  echo "warning: $msg" >&2
  for ref in refs/heads/release refs/remotes/origin/release; do
    if git rev-parse -q --verify "$ref^{commit}" >/dev/null; then
      src_ref=$ref from=${ref#refs/*/}
      break
    fi
  done
  [[ -n $src_ref ]] || echo "warning: no release branch here: the versions below come from '$branch'" >&2
fi

if ((!dry)) && { ! git diff --quiet || ! git diff --cached --quiet; }; then
  die "the working tree has uncommitted changes (commit or stash them first)"
fi

cmake_file=CMakeLists.txt
nix_file=nix/package.nix
version_file() { # <path>: the file as the release would start from it
  if [[ -n $src_ref ]]; then git show "$src_ref:$1"; else cat -- "$1"; fi
}
cmake_text=$(version_file "$cmake_file") || die "cannot read $cmake_file"
nix_text=$(version_file "$nix_file") || die "cannot read $nix_file"
current=$(sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+)[[:space:]]*$/\1/p' <<<"$cmake_text" | head -n 1)
[[ $current =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]] || die "no project VERSION X.Y.Z found in $cmake_file"
major=${BASH_REMATCH[1]} minor=${BASH_REMATCH[2]} patch=${BASH_REMATCH[3]}
nix_current=$(sed -nE 's/^[[:space:]]*version = "([^"]+)";.*/\1/p' <<<"$nix_text" | head -n 1)
[[ -n $nix_current ]] || die "no version = \"...\"; found in $nix_file"
[[ $nix_current == "$current" ]] ||
  echo "warning: $nix_file has version $nix_current, $cmake_file $current; both get the new version" >&2

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

tag=v$new
if git rev-parse -q --verify "refs/tags/$tag" >/dev/null; then die "tag $tag already exists"; fi
# One round trip to origin: the tag, and the branches whose commits the release must carry.
remote_refs='' reached=0
if git remote get-url origin >/dev/null 2>&1; then
  if remote_refs=$(git ls-remote origin refs/heads/main refs/heads/release "refs/tags/$tag" 2>/dev/null); then
    reached=1
  else
    echo "warning: origin is not reachable: comparing with the last fetched origin/main and origin/release" >&2
  fi
fi
remote_commit() { awk -v ref="$1" '$2 == ref { print $1; exit }' <<<"$remote_refs"; }
[[ -z $(remote_commit "refs/tags/$tag") ]] || die "tag $tag already exists on origin"

# The release must carry everything on main (local and origin's) and on origin/release: after a
# forgotten `git merge main` it would ship without the newest fixes, and an origin/release that
# moved on would reject the push only after the tag was made.
target=${src_ref:-HEAD}
stale=()
lacks() { # <name> <commit, empty when there is none> <how to merge it>
  [[ -n $2 ]] || return 0
  local n
  if ! git cat-file -e "$2^{commit}" 2>/dev/null; then
    stale+=("$1: commits not fetched yet ($3)")
  elif ! git merge-base --is-ancestor "$2" "$target"; then
    n=$(git rev-list --count "$target..$2")
    if ((n == 1)); then stale+=("$1: 1 commit ($3)"); else stale+=("$1: $n commits ($3)"); fi
  fi
}
known() { git rev-parse -q --verify "$1^{commit}" || true; }
lacks main "$(known refs/heads/main)" "git merge main"
if ((reached)); then
  lacks origin/main "$(remote_commit refs/heads/main)" "git fetch origin && git merge origin/main"
else
  lacks origin/main "$(known refs/remotes/origin/main)" "git merge origin/main"
fi
# Main's commits may wait for the next release (--allow-stale); origin/release's may not: origin
# would reject the push of the release branch, after the release commit and the tag were made.
on_main=${#stale[@]}
if ((reached)); then
  lacks origin/release "$(remote_commit refs/heads/release)" "git fetch origin && git merge origin/release"
else
  lacks origin/release "$(known refs/remotes/origin/release)" "git merge origin/release"
fi
behind=$((${#stale[@]} > on_main))
if ((${#stale[@]})); then
  report="'$from' lacks commits that are on"$'\n'"$(printf '  %s\n' "${stale[@]}")"
  if ((!dry && behind)); then
    die "$report"$'\n'"Merge them first (--allow-stale does not skip origin/release: origin would reject the push)."
  elif ((!dry && !allow_stale)); then
    die "$report"$'\n'"Merge them first, or pass --allow-stale to release without them."
  fi
  echo "warning: $report" >&2
  if ((dry && behind)); then
    echo "A real run refuses to release without them." >&2
  elif ((dry)); then
    echo "A real run refuses to release without them (unless --allow-stale)." >&2
  else
    echo "Releasing without them (--allow-stale)." >&2
  fi
fi

# The notes are published as the release's description: unchanged ones repeat the last release's.
notes=.github/release-notes.md
if last=$(git describe --tags --abbrev=0 "$target" 2>/dev/null) &&
  git diff --quiet "$last" ${src_ref:+"$src_ref"} -- "$notes"; then
  echo "warning: $notes has not changed since $last (commit \"Update release notes for $tag\" first)" >&2
fi

files=()
if [[ $release != "$current" || $nix_current != "$release" ]]; then
  files=("$cmake_file" "$nix_file")
fi
echo "Brulion $current -> $new (tag $tag, from $from)"
if ((${#files[@]})); then
  echo "Updates ${files[*]} to $release, commits \"Release $tag\" and tags it."
else
  echo "$cmake_file already has $release: only the tag is created."
fi
((dry)) && exit 0

if ((!yes)) && [[ -t 0 ]]; then
  read -r -p "Release $tag? [y/N] " answer
  [[ $answer == [yY]* ]] || die "aborted"
fi

if ((${#files[@]})); then
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
git tag -a "$tag" -m "Brulion $new"
echo "Created tag $tag at $(git rev-parse --short HEAD)."

# Always the branch with the tag: also a tag-only release has the merge and notes commits.
if ((push)); then
  git push --atomic origin HEAD "refs/tags/$tag"
  echo "Pushed. Now bring the version and the notes back to main:"
else
  echo "Publish it with:  git push --atomic origin release $tag"
  echo "Then bring the version and the notes back to main:"
fi
echo "  git switch main && git merge --ff-only release   # main moved on: git merge --no-edit release"
echo "  git push origin main"
