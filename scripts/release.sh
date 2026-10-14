#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Bump the Brulion version everywhere it is recorded, commit, and create the tag v<version>.
# Pushing the tag starts .github/workflows/release.yml (Linux bundles, Android APK, web zip).
#
# Usage: scripts/release.sh <X.Y.Z[-pre] | major | minor | patch> [--push] [--yes]
#   --push   push the commit and the tag to origin right away
#   --yes    do not ask for confirmation
#
# Updated files: package.json, package-lock.json, src-tauri/Cargo.toml, src-tauri/Cargo.lock,
# nix/npm-deps.nix (its hash covers package-lock.json, version field included; recomputed with
# scripts/update-nix-hashes.sh when Nix is available) and a literal `version = "<old>";` in
# flake.nix / nix/*.nix. tauri.conf.json takes its
# version from package.json; the Android versionCode is derived from it as
# major*1000000 + minor*1000 + patch, so minor and patch must stay below 1000.
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

die() { printf 'error: %s\n' "$*" >&2; exit 1; }
usage() { awk 'NR > 2 && /^#/ { sub(/^# ?/, ""); print; next } NR > 2 { exit }' "${BASH_SOURCE[0]}"; }

bump='' push=0 yes=0
for arg in "$@"; do
  case $arg in
    --push) push=1 ;;
    --yes | -y) yes=1 ;;
    -h | --help) usage; exit 0 ;;
    -*) die "unknown option $arg" ;;
    *) [[ -z $bump ]] || die "more than one version given"; bump=$arg ;;
  esac
done
[[ -n $bump ]] || { usage >&2; exit 2; }

git rev-parse --is-inside-work-tree >/dev/null 2>&1 || die "not a git repository"
if ! git diff --quiet || ! git diff --cached --quiet; then
  die "the working tree has uncommitted changes"
fi

current=$(node -p "require('./package.json').version")
semver='^([0-9]+)\.([0-9]+)\.([0-9]+)(-[0-9A-Za-z.-]+)?$'
[[ $current =~ $semver ]] || die "package.json has an unexpected version: $current"
major=${BASH_REMATCH[1]} minor=${BASH_REMATCH[2]} patch=${BASH_REMATCH[3]} pre=${BASH_REMATCH[4]}

case $bump in
  major) new=$((major + 1)).0.0 ;;
  minor) new=$major.$((minor + 1)).0 ;;
  patch) if [[ -n $pre ]]; then new=$major.$minor.$patch; else new=$major.$minor.$((patch + 1)); fi ;;
  *) new=${bump#v} ;;
esac
[[ $new =~ $semver ]] || die "not a semantic version: $new"
((10#${BASH_REMATCH[2]} < 1000 && 10#${BASH_REMATCH[3]} < 1000)) ||
  die "minor and patch must be below 1000 (Android versionCode)"
[[ $new != "$current" ]] || die "the version is already $current"
tag=v$new
git rev-parse -q --verify "refs/tags/$tag" >/dev/null && die "tag $tag already exists"

branch=$(git symbolic-ref --short -q HEAD || echo "(detached)")
[[ $branch == main ]] || echo "warning: releasing from '$branch', not main" >&2

if ((!yes)) && [[ -t 0 ]]; then
  read -r -p "Release $tag (currently $current) from $branch? [y/N] " answer
  [[ $answer == [yY]* ]] || die "aborted"
fi

# package.json + package-lock.json (no git commit/tag from npm, no lifecycle surprises).
npm version "$new" --no-git-tag-version --ignore-scripts >/dev/null

# src-tauri/Cargo.toml: the `version` key of the [package] table only.
awk -v new="$new" '
  /^\[/ { in_pkg = ($0 == "[package]") }
  in_pkg && !done && /^version[[:space:]]*=/ { sub(/"[^"]*"/, "\"" new "\""); done = 1 }
  { print }
  END { if (!done) exit 3 }
' src-tauri/Cargo.toml >src-tauri/Cargo.toml.tmp || die "no [package] version in src-tauri/Cargo.toml"
mv src-tauri/Cargo.toml.tmp src-tauri/Cargo.toml

# src-tauri/Cargo.lock: the entry of the brulion package itself (CI builds with --locked).
if [[ -f src-tauri/Cargo.lock ]]; then
  awk -v new="$new" '
    /^\[\[package\]\]/ { is_app = 0 }
    /^name = "brulion"$/ { is_app = 1 }
    is_app && /^version = / { sub(/"[^"]*"/, "\"" new "\""); is_app = 0; done = 1 }
    { print }
    END { if (!done) exit 3 }
  ' src-tauri/Cargo.lock >src-tauri/Cargo.lock.tmp || die "brulion not found in src-tauri/Cargo.lock"
  mv src-tauri/Cargo.lock.tmp src-tauri/Cargo.lock
fi

# Nix expressions that spell the version out literally.
nix_files=()
for f in flake.nix nix/*.nix; do
  if [[ -f $f ]] && grep -qF "version = \"$current\";" "$f"; then nix_files+=("$f"); fi
done
for f in "${nix_files[@]}"; do
  sed -i "s/version = \"${current//./\\.}\";/version = \"$new\";/" "$f"
done

# The npm dependency hash of the Nix packages covers package-lock.json (version included), so
# `nix build` fails until it is recomputed.
npm_deps=nix/npm-deps.nix
if [[ -f $npm_deps ]]; then
  if command -v nix >/dev/null 2>&1; then
    scripts/update-nix-hashes.sh ||
      die "could not update $npm_deps; undo the bump with: git checkout -- ."
    if ! git diff --quiet -- "$npm_deps"; then nix_files+=("$npm_deps"); fi
  else
    echo "warning: nix is not installed, $npm_deps was not updated: run scripts/update-nix-hashes.sh" \
      "and commit it, or the Nix build (CI job 'Nix flake') fails" >&2
  fi
fi

files=(package.json package-lock.json src-tauri/Cargo.toml)
[[ -f src-tauri/Cargo.lock ]] && files+=(src-tauri/Cargo.lock)
files+=("${nix_files[@]}")
git --no-pager diff --stat -- "${files[@]}"

git commit -q -m "Release $tag" -- "${files[@]}"
git tag -a "$tag" -m "Brulion $new"
echo "Created commit $(git rev-parse --short HEAD) and tag $tag."

if ((push)); then
  git push --atomic origin HEAD "refs/tags/$tag"
else
  echo "Publish it with:  git push --atomic origin HEAD $tag"
fi
