#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Builds OpenSSL 3 for Android from source with the NDK (r27c): libssl_3.so and libcrypto_3.so,
# the names Qt's TLS plugin loads at run time, in the layout BRULION_ANDROID_OPENSSL_DIR expects
# (src/app/CMakeLists.txt, the same as KDAB/android_openssl):
#   <out>/ssl_3/<abi>/libssl_3.so, libcrypto_3.so   (stripped; 64-bit ABIs: 16 KB pages)
#   <out>/ssl_3/<abi>/include/openssl/*.h            (headers for Qt's own build, android-qt-from-source.sh)
# Used instead of KDAB's prebuilt libraries for builds from source (F-Droid): see docs/ANDROID.md.
# Idempotent: an ABI whose libraries exist (with a matching stamp) is not built again.
#
# Usage: scripts/android-openssl-from-source.sh [--help]
#
# Environment (all optional):
#   BRULION_OPENSSL_OUT         output directory (default ~/.cache/brulion-openssl/<version>);
#                               pass it to android-build.sh as BRULION_ANDROID_OPENSSL_DIR
#   BRULION_OPENSSL_SOURCE_DIR  an OpenSSL source tree at tag openssl-<version> (e.g. F-Droid's
#                               srclib OpenSSL@openssl-3.5.9) instead of the pinned release archive
#   BRULION_OPENSSL_DOWNLOADS   where the archive is downloaded (default ~/.cache/brulion-src-downloads)
#   BRULION_ANDROID_ABIS        ABIs, space separated (default "arm64-v8a"; also armeabi-v7a, x86_64, x86)
#   ANDROID_NDK_ROOT            the NDK (default $ANDROID_SDK_ROOT/ndk/$NDK_VERSION,
#                               ANDROID_SDK_ROOT defaults like android-build.sh)
#   NDK_VERSION                 default 27.2.12479018 (r27c)
#   JOBS                        parallel make jobs (default: all cores)
# Needs perl and make (on NixOS the script re-runs itself in `nix shell` with them).
#
# Reproducible: the build runs inside a copy of the sources (relative file names in OPENSSL_FILE,
# no debug info: the build path appears nowhere), SOURCE_DATE_EPOCH is fixed (the "built on" date
# OpenSSL records) and the libraries are stripped, so the same NDK gives the same files in any
# directory (checked: two output directories, identical SHA-256).
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
SCRIPT="$ROOT/scripts/$(basename -- "${BASH_SOURCE[0]}")"

info() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
die() {
  if [[ -n ${GITHUB_ACTIONS:-} ]]; then echo "::error::$*"; fi
  printf '\033[1;31merror:\033[0m %s\n' "$*" >&2
  exit 1
}

case ${1:-} in
  -h | --help)
    awk 'NR > 2 && /^#/ { sub(/^# ?/, ""); print; next } NR > 2 { exit }' "$SCRIPT"
    exit 0
    ;;
  "") ;;
  *) die "unknown argument: $1 (see --help)" ;;
esac

# OpenSSL 3.5 is the LTS series (supported until 2030-04). Update: the version and the SHA-256
# that https://github.com/openssl/openssl/releases publishes next to the .tar.gz.
OPENSSL_VERSION=3.5.9
OPENSSL_SHA256=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
# "built on" date compiled into libcrypto (OpenSSL_version(OPENSSL_BUILT_ON)): the release day of
# OpenSSL 3.5.9 (2026-09-29), not the build time, so that every build gives the same bytes.
OPENSSL_EPOCH=1790640000
# Android API level the libraries are built for: the app's minSdkVersion (src/app/CMakeLists.txt).
ANDROID_API=28

[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || die "needs an x86_64 Linux host (the NDK is x86_64 only)"

if [[ -e /etc/NIXOS && -z ${BRULION_OPENSSL_IN_NIX_SHELL:-} ]]; then
  info "NixOS: entering nix shell (perl, make)"
  export BRULION_OPENSSL_IN_NIX_SHELL=1
  exec nix shell nixpkgs#perl nixpkgs#gnumake --command bash "$SCRIPT" "$@"
fi
{ command -v perl && command -v make; } >/dev/null || die "perl and make are needed"

NDK_VERSION=${NDK_VERSION:-27.2.12479018}
SDK=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/.cache/brulion-android-sdk}}
NDK=${ANDROID_NDK_ROOT:-$SDK/ndk/$NDK_VERSION}
[[ -f $NDK/source.properties ]] || die "Android NDK not found at $NDK (set ANDROID_NDK_ROOT)"
NDK=$(cd -- "$NDK" && pwd)
ndk_revision=$(sed -nE 's/^Pkg\.Revision *= *([0-9.]+).*/\1/p' "$NDK/source.properties")
TOOLCHAIN=$NDK/toolchains/llvm/prebuilt/linux-x86_64
[[ -x $TOOLCHAIN/bin/clang ]] || die "$TOOLCHAIN/bin/clang not found"
read -r -a ABIS <<<"${BRULION_ANDROID_ABIS:-arm64-v8a}"
((${#ABIS[@]})) || die "BRULION_ANDROID_ABIS is empty"
OUT=${BRULION_OPENSSL_OUT:-$HOME/.cache/brulion-openssl/$OPENSSL_VERSION}
DOWNLOADS=${BRULION_OPENSSL_DOWNLOADS:-$HOME/.cache/brulion-src-downloads}
JOBS=${JOBS:-$(nproc)}
mkdir -p "$OUT"
OUT=$(cd -- "$OUT" && pwd)

openssl_target() { # ABI -> OpenSSL's Configure target
  case $1 in
    arm64-v8a) echo android-arm64 ;;
    armeabi-v7a) echo android-arm ;;
    x86_64) echo android-x86_64 ;;
    x86) echo android-x86 ;;
    *) die "unknown Android ABI: $1" ;;
  esac
}

info "OpenSSL $OPENSSL_VERSION for Android ${ABIS[*]} (API $ANDROID_API, NDK $ndk_revision) -> $OUT/ssl_3"

# --- sources ------------------------------------------------------------------------------------

if [[ -n ${BRULION_OPENSSL_SOURCE_DIR:-} ]]; then
  SRC=$(cd -- "$BRULION_OPENSSL_SOURCE_DIR" && pwd) || die "BRULION_OPENSSL_SOURCE_DIR=$BRULION_OPENSSL_SOURCE_DIR does not exist"
  [[ -f $SRC/VERSION.dat && -x $SRC/Configure ]] || die "$SRC is not an OpenSSL 3 source tree (no VERSION.dat / Configure)"
  src_version=$(awk -F= '/^MAJOR=/ { a = $2 } /^MINOR=/ { b = $2 } /^PATCH=/ { c = $2 } END { print a "." b "." c }' "$SRC/VERSION.dat")
  [[ $src_version == "$OPENSSL_VERSION" ]] || die "$SRC is OpenSSL $src_version, this script pins $OPENSSL_VERSION"
  src_id="source tree $SRC"
else
  archive=$DOWNLOADS/openssl-$OPENSSL_VERSION.tar.gz
  if [[ ! -f $archive ]] || ! echo "$OPENSSL_SHA256  $archive" | sha256sum -c --quiet - 2>/dev/null; then
    mkdir -p "$DOWNLOADS"
    url=https://github.com/openssl/openssl/releases/download/openssl-$OPENSSL_VERSION/openssl-$OPENSSL_VERSION.tar.gz
    info "Downloading $url"
    curl -fL --retry 3 -o "$archive.part" "$url" || die "cannot download $url"
    mv -- "$archive.part" "$archive"
  fi
  echo "$OPENSSL_SHA256  $archive" | sha256sum -c --quiet - ||
    die "$archive: SHA-256 $(sha256sum "$archive" | cut -d' ' -f1), expected $OPENSSL_SHA256"
  SRC=
  src_id="openssl-$OPENSSL_VERSION.tar.gz sha256 $OPENSSL_SHA256"
fi

# --- build per ABI ------------------------------------------------------------------------------

work=$OUT/.build
export ANDROID_NDK_ROOT=$NDK ANDROID_NDK_HOME=$NDK
export PATH="$TOOLCHAIN/bin:$PATH"
export SOURCE_DATE_EPOCH=$OPENSSL_EPOCH
# Same flags wherever the build runs: no locale- or user-dependent tool output.
export LC_ALL=C TZ=UTC

for abi in "${ABIS[@]}"; do
  target=$(openssl_target "$abi")
  dest=$OUT/ssl_3/$abi
  stamp="openssl $OPENSSL_VERSION $target api $ANDROID_API ndk $ndk_revision ($src_id)"
  if [[ -f $dest/libssl_3.so && -f $dest/libcrypto_3.so && -f $dest/.stamp && $(<"$dest/.stamp") == "$stamp" ]]; then
    echo "$abi: up to date ($dest)"
    continue
  fi
  info "Building OpenSSL $OPENSSL_VERSION for $abi ($target)"
  dir=$work/$abi/openssl
  rm -rf -- "${work:?}/$abi"
  mkdir -p "$work/$abi"
  if [[ -n $SRC ]]; then
    # A copy without the VCS data: OpenSSL builds in its source tree (relative file names).
    mkdir -p "$dir"
    tar -C "$SRC" --exclude=.git -cf - . | tar -C "$dir" -xf -
  else
    tar -C "$work/$abi" -xzf "$archive"
    mv -- "$work/$abi/openssl-$OPENSSL_VERSION" "$dir"
  fi

  # Qt loads libssl_3.so / libcrypto_3.so (Qt for Android's convention): a target inheriting
  # OpenSSL's Android one with shlib_variant "_3", plus an empty SHLIB_VERSION_NUMBER at make time
  # for unversioned file names (what KDAB's build_ssl.sh does with a patch).
  cat >"$work/$abi/brulion-android.conf" <<EOF
my %targets = (
    "brulion-$target" => {
        inherit_from  => [ "$target" ],
        shlib_variant => "_3",
    },
);
EOF
  config=(
    "brulion-$target" shared
    no-tests no-docs
    -U__ANDROID_API__ "-D__ANDROID_API__=$ANDROID_API"
    --prefix=/usr/local --openssldir=/usr/local/ssl
  )
  case $abi in
    arm64-v8a | x86_64) config+=("-Wl,-z,max-page-size=16384" "-Wl,-z,common-page-size=16384") ;;
  esac
  (
    cd -- "$dir"
    OPENSSL_LOCAL_CONFIG_DIR=$work/$abi ./Configure "${config[@]}" >"$work/$abi/configure.log" 2>&1 ||
      {
        tail -n 40 "$work/$abi/configure.log" >&2
        exit 1
      }
    make -j"$JOBS" SHLIB_VERSION_NUMBER= build_libs >"$work/$abi/build.log" 2>&1 ||
      {
        tail -n 60 "$work/$abi/build.log" >&2
        exit 1
      }
  ) || die "OpenSSL build for $abi failed (logs: $work/$abi/*.log)"

  rm -rf -- "$dest"
  mkdir -p "$dest/include"
  for lib in libssl_3.so libcrypto_3.so; do
    [[ -f $dir/$lib ]] || die "$lib was not built for $abi (in $dir)"
    llvm-strip --strip-all -o "$dest/$lib" "$dir/$lib"
  done
  cp -r -- "$dir/include/openssl" "$dest/include/"
  find "$dest/include" \( -name '*.in' -o -name '*.def' \) -delete

  # The checks Qt and Android need: Qt dlopen()s these names, the system loader resolves NEEDED
  # entries by SONAME, and 64-bit Android 15+ devices with 16 KB pages refuse smaller alignment.
  readelf=$TOOLCHAIN/bin/llvm-readelf
  for lib in libssl_3.so libcrypto_3.so; do
    soname=$("$readelf" -dW "$dest/$lib" | sed -nE 's/.*\(SONAME\).*\[(.*)\].*/\1/p')
    [[ $soname == "$lib" ]] || die "$dest/$lib has SONAME '$soname', expected $lib"
  done
  needed=$("$readelf" -dW "$dest/libssl_3.so" | sed -nE 's/.*\(NEEDED\).*\[(.*)\].*/\1/p' | tr '\n' ' ')
  [[ $needed == *libcrypto_3.so* ]] || die "libssl_3.so does not need libcrypto_3.so (NEEDED: $needed)"
  if [[ $abi == arm64-v8a || $abi == x86_64 ]]; then
    for lib in libssl_3.so libcrypto_3.so; do
      while read -r align; do
        (($((align)) >= 16384)) || die "$dest/$lib: LOAD segment aligned to $align, 16 KB pages need 0x4000"
      done < <("$readelf" -lW "$dest/$lib" | awk '$1 == "LOAD" { print $NF }')
    done
  fi
  version=$(LC_ALL=C grep -a -o -m 1 "OpenSSL $OPENSSL_VERSION [0-9]* [A-Z][a-z]* [0-9]*" "$dest/libcrypto_3.so" || true)
  [[ -n $version ]] || die "$dest/libcrypto_3.so does not contain the version string of OpenSSL $OPENSSL_VERSION"
  printf '%s\n' "$stamp" >"$dest/.stamp"
  rm -rf -- "${work:?}/$abi"
  echo "$abi: $version, NEEDED $needed"
  (cd -- "$dest" && sha256sum libssl_3.so libcrypto_3.so)
done
rmdir -- "$work" 2>/dev/null || true

info "Done: BRULION_ANDROID_OPENSSL_DIR=$OUT"
