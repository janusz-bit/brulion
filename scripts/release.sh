#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Sets the Brulion version where it is recorded, commits, and creates the annotated tag
# v<version>. Pushing the tag starts .github/workflows/release.yml (Android APK, Linux
# AppImages, web zip, source tarball -> GitHub release).
#
# Usage: scripts/release.sh <X.Y.Z[-pre] | major | minor | patch> [--push] [--yes] [--dry-run]
#   X.Y.Z[-pre]  the new version; a suffix (0.2.0-rc.1) only marks the tag as a pre-release
#                (CMake and the Android versionName keep 0.2.0)
#   major|minor|patch  bump that part of the current version
#   --push     push the commit and the tag to origin right away (atomically)
#   --yes      do not ask for confirmation
#   --dry-run  show what would change, change nothing
#
# Updated files: CMakeLists.txt (project VERSION — the app, the Android versionName and
# versionCode = major*1000000 + minor*1000 + patch, so minor and patch stay below 1000) and
# nix/package.nix (`version = "...";`). Releasing the version that is already recorded (e.g. the
# first release, or a further pre-release) only creates the tag.
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}
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
if ((!dry)) && { ! git diff --quiet || ! git diff --cached --quiet; }; then
  die "the working tree has uncommitted changes (commit or stash them first)"
fi

cmake_file=CMakeLists.txt
nix_file=nix/package.nix
current=$(sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+)[[:space:]]*$/\1/p' "$cmake_file" | head -n 1)
[[ $current =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]] || die "no project VERSION X.Y.Z found in $cmake_file"
major=${BASH_REMATCH[1]} minor=${BASH_REMATCH[2]} patch=${BASH_REMATCH[3]}
nix_current=$(sed -nE 's/^[[:space:]]*version = "([^"]+)";.*/\1/p' "$nix_file" | head -n 1)
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
if git remote get-url origin >/dev/null 2>&1 &&
  git ls-remote --exit-code --tags origin "refs/tags/$tag" >/dev/null 2>&1; then
  die "tag $tag already exists on origin"
fi

branch=$(git symbolic-ref --short -q HEAD || echo "(detached)")
[[ $branch == main ]] || echo "warning: releasing from '$branch', not main" >&2

files=()
if [[ $release != "$current" || $nix_current != "$release" ]]; then
  files=("$cmake_file" "$nix_file")
fi
echo "Brulion $current -> $new (tag $tag, from $branch)"
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

if ((push)); then
  if ((${#files[@]})); then
    git push --atomic origin HEAD "refs/tags/$tag"
  else
    git push origin "refs/tags/$tag"
  fi
else
  if ((${#files[@]})); then
    echo "Publish it with:  git push --atomic origin HEAD $tag"
  else
    echo "Publish it with:  git push origin $tag"
  fi
fi
