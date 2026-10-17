#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Updates the locks of the Nix Android build (nix/android.nix, `nix build .#brulion-android`):
#   qt-android.json   Qt for Android archives and their SHA-256, for the Qt version of the
#                     flake's nixpkgs (qt6.qtbase: the host tools must be the same version)
#   openssl.json      the hash of the pinned KDAB android_openssl commit (--openssl REV)
#   gradle-deps.json  Gradle's downloads (Android Gradle Plugin, Kotlin, androidx…), recorded by
#                     mitm-cache while the build runs up to `gradle assembleRelease` with network
#                     access (outside the sandbox, in bubblewrap; a few minutes)
# Run it after `nix flake update` (a new Qt in nixpkgs) and after changes to Gradle
# dependencies (Qt's Android template, android/), then build: nix build .#brulion-android
#
# Usage: nix/android/update-locks.sh [--qt] [--gradle] [--openssl REV] [--modules "qtbase …"]
#   default: --qt --gradle. Needs an x86_64 Linux with Nix (flakes enabled).
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$ROOT"
DIR=nix/android

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}
info() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }

do_qt=0 do_gradle=0 openssl_rev='' modules=''
while (($#)); do
  case $1 in
    --qt) do_qt=1 ;;
    --gradle) do_gradle=1 ;;
    --openssl) openssl_rev=${2:?--openssl needs a commit} && shift ;;
    --modules) modules=${2:?--modules needs a list} && do_qt=1 && shift ;;
    -h | --help)
      awk 'NR > 2 && /^#/ { sub(/^# ?/, ""); print; next } NR > 2 { exit }' "${BASH_SOURCE[0]}"
      exit 0
      ;;
    *) die "unknown argument: $1 (see --help)" ;;
  esac
  shift
done
if ((!do_qt && !do_gradle)) && [[ -z $openssl_rev ]]; then
  do_qt=1 do_gradle=1
fi
[[ $(uname -sm) == "Linux x86_64" ]] || die "needs an x86_64 Linux (the NDK and the SDK build tools are x86_64 binaries)"

nix_() { nix --extra-experimental-features 'nix-command flakes' "$@"; }

if ((do_qt)); then
  qt_version=$(nix_ eval --raw --inputs-from . nixpkgs#qt6.qtbase.version)
  info "Qt for Android $qt_version (the flake's nixpkgs qt6.qtbase) -> $DIR/qt-android.json"
  current_modules=$(nix_ eval --raw --impure --expr \
    "builtins.concatStringsSep \" \" (builtins.fromJSON (builtins.readFile ./$DIR/qt-android.json)).modules")
  nix_ shell --inputs-from . nixpkgs#python3 --command python3 "$DIR/update-qt-lock.py" \
    "$qt_version" "$DIR/qt-android.json" --modules "${modules:-$current_modules}"
fi

if [[ -n $openssl_rev ]]; then
  info "KDAB android_openssl $openssl_rev -> $DIR/openssl.json"
  # The archive fetchFromGitHub downloads (unpacked: the hash of its NAR).
  url=https://github.com/KDAB/android_openssl/archive/$openssl_rev.tar.gz
  hash=$(nix_ store prefetch-file --json --unpack "$url" |
    nix_ shell --inputs-from . nixpkgs#jq --command jq -r .hash)
  [[ $hash == sha256-* ]] || die "cannot prefetch $url"
  tmp=$(mktemp)
  # shellcheck disable=SC2016 # jq's variables
  nix_ shell --inputs-from . nixpkgs#jq --command jq --arg rev "$openssl_rev" --arg hash "$hash" \
    '.rev = $rev | .hash = $hash' "$DIR/openssl.json" >"$tmp"
  mv -- "$tmp" "$DIR/openssl.json"
fi

if ((do_gradle)); then
  info "Gradle dependencies (mitm-cache) -> $DIR/gradle-deps.json"
  # The update script of gradle.fetchDeps: builds the app up to Gradle in a nix-shell of the
  # package (with network access through a recording proxy) and writes the lock relative to
  # the current directory (the repository root).
  script=$(nix_ build --no-link --print-out-paths .#brulion-android.mitmCache.updateScript)
  "$script"
fi

info "Done"
git -C "$ROOT" diff --stat -- "$DIR" 2>/dev/null || true
echo "Next: nix build .#brulion-android -L"
