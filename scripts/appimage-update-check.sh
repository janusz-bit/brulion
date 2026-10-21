#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Checks that the AppImages among a release's files can update themselves. AppImageUpdate, Gear
# Lever and AppImageLauncher read the update information embedded in an AppImage
# (gh-releases-zsync|<owner>|<repo>|latest|<file pattern>), fetch the one .zsync file of the
# latest GitHub release that matches the pattern, and download the file that the .zsync names.
# So every *.AppImage needs its own <name>.zsync next to it that names it, and its pattern must
# match exactly that .zsync among the files. .github/workflows/release.yml runs this before it
# uploads the files.
#
# Usage: scripts/appimage-update-check.sh <directory> <owner/repo>
# Needs readelf (binutils).
set -euo pipefail

die() {
  if [[ -n ${GITHUB_ACTIONS:-} ]]; then echo "::error::$*"; fi
  printf 'error: %s\n' "$*" >&2
  exit 1
}

(($# == 2)) && [[ -d $1 && $2 == */* ]] || die "usage: $0 <directory> <owner/repo>"
dir=$1 owner=${2%%/*} repo=${2#*/}
shopt -s nullglob
appimages=("$dir"/*.AppImage)
((${#appimages[@]})) || die "no *.AppImage in $dir"

for f in "${appimages[@]}"; do
  name=$(basename -- "$f")
  [[ -f $f.zsync ]] || die "$name.zsync is missing (scripts/appimage-build.sh writes it next to the AppImage)"
  info=$(readelf --wide --string-dump=.upd_info "$f" 2>/dev/null |
    sed -nE 's/^[[:space:]]*\[[[:space:]]*0\][[:space:]]+//p') || info=''
  [[ ${info%|*} == "gh-releases-zsync|$owner|$repo|latest" ]] ||
    die "$name has the update information '$info', not gh-releases-zsync|$owner|$repo|latest|<file pattern>"
  pattern=${info##*|} matches=()
  for z in "$dir"/*.zsync; do
    # shellcheck disable=SC2053 # $pattern is a glob
    if [[ $(basename -- "$z") == $pattern ]]; then matches+=("$(basename -- "$z")"); fi
  done
  [[ ${matches[*]} == "$name.zsync" ]] ||
    die "the update pattern $pattern of $name matches '${matches[*]}', not just $name.zsync"
  grep -qxF -- "URL: $name" "$f.zsync" || die "$name.zsync does not name $name (no 'URL: $name' line)"
  echo "$name: updates from $pattern ($name.zsync)"
done
