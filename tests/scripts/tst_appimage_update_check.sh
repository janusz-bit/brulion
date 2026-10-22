#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Tests scripts/appimage-update-check.sh on release directories with stand-in AppImages: ELF
# files with an .upd_info section like the AppImage runtime's (made with objcopy).
#
# Usage: tests/scripts/tst_appimage_update_check.sh [path/to/appimage-update-check.sh]
set -euo pipefail

script=$(realpath -- "${1:-$(dirname -- "${BASH_SOURCE[0]}")/../../scripts/appimage-update-check.sh}")
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT
unset GITHUB_ACTIONS
elf=$(readlink -f -- "$BASH")
v=0.3.6

failed=0
check() { # <description> <expected: ok|fail> <directory> [owner/repo]
  local status=0 got=ok
  out=$(bash "$script" "$3" "${4:-janusz-bit/brulion}" 2>&1) || status=$?
  ((status == 0)) || got=fail
  printf '%s\n' "$out" | sed 's/^/        | /'
  if [[ $got == "$2" ]]; then
    printf 'PASS   : %s\n' "$1"
  else
    printf 'FAIL!  : %s (expected %s, got %s)\n' "$1" "$2" "$got"
    failed=$((failed + 1))
  fi
}

appimage() { # <file> <update information, empty for none>
  if [[ -z $2 ]]; then
    cp -- "$elf" "$1"
    return
  fi
  printf '%s' "$2" >"$tmp/upd_info"
  truncate -s 1024 "$tmp/upd_info" # the runtime reserves 1024 bytes
  objcopy --add-section .upd_info="$tmp/upd_info" "$elf" "$1"
}
zsync() { # <file> <AppImage name it refers to>
  printf 'zsync: 0.6.2\nFilename: %s\nBlocksize: 2048\nURL: %s\nSHA-1: 0\n\n' "$2" "$2" >"$1"
}
info() { echo "gh-releases-zsync|janusz-bit|brulion|latest|Brulion-*-$1.AppImage.zsync"; }

# release <name>: a directory like the release's assets, with both AppImages and their .zsync.
release() {
  dir=$tmp/$1
  mkdir -p "$dir"
  echo apk >"$dir/brulion-$v-arm64-v8a.apk"
  for arch in x86_64 aarch64; do
    appimage "$dir/Brulion-$v-$arch.AppImage" "$(info "$arch")"
    zsync "$dir/Brulion-$v-$arch.AppImage.zsync" "Brulion-$v-$arch.AppImage"
  done
}

release good
check "both AppImages with their own .zsync" ok "$dir"
expect_line="Brulion-$v-aarch64.AppImage: updates from Brulion-*-aarch64.AppImage.zsync (Brulion-$v-aarch64.AppImage.zsync)"
if grep -qxF -- "$expect_line" <<<"$out"; then echo "PASS   : ... and says so"; else
  echo "FAIL!  : ... and says so"
  failed=$((failed + 1))
fi

release missing
rm -- "$dir/Brulion-$v-aarch64.AppImage.zsync"
check "a .zsync is missing (as in v0.1.0 to v0.3.5)" fail "$dir"

release fork
check "the update information names another repository" fail "$dir" someone/brulion

release both
appimage "$dir/Brulion-$v-aarch64.AppImage" "gh-releases-zsync|janusz-bit|brulion|latest|Brulion-*.AppImage.zsync"
check "a pattern that matches both architectures" fail "$dir"

release crossed
appimage "$dir/Brulion-$v-aarch64.AppImage" "$(info x86_64)"
check "aarch64 updating from the x86_64 file" fail "$dir"

release tagged
appimage "$dir/Brulion-$v-x86_64.AppImage" "gh-releases-zsync|janusz-bit|brulion|v0.3.5|Brulion-*-x86_64.AppImage.zsync"
check "a fixed tag instead of latest" fail "$dir"

release renamed
zsync "$dir/Brulion-$v-aarch64.AppImage.zsync" "Brulion-0.3.5-aarch64.AppImage"
check "a .zsync naming another AppImage" fail "$dir"

release plain
appimage "$dir/Brulion-$v-x86_64.AppImage" ""
check "an AppImage without update information" fail "$dir"

mkdir "$tmp/empty"
check "no AppImages at all" fail "$tmp/empty"

echo
if ((failed)); then
  echo "Totals: $failed failed"
  exit 1
fi
echo "Totals: all passed"
