#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Pins the LLVM project's release of a Clang version for the Nix builds (nix/clang-release.json;
# see nix/clang-release.nix): for x86_64-linux and aarch64-linux, the release archive's name and
# SHA-256 and the Nix hash of the one file kept from it, bin/clang-<major>. Run it when flake.lock
# moves to a nixpkgs with another Clang (checks.<system>.clang-release fails until then):
#   scripts/clang-release-pin.sh "$(nix eval --raw .#packages.x86_64-linux.brulion.stdenv.cc.cc.version)"
#
# Usage: scripts/clang-release-pin.sh <version>          e.g. scripts/clang-release-pin.sh 21.1.8
# Each archive is 1.9 GB; it is unpacked while it downloads and only the compiler (0.3 GB) touches
# the disk, in a folder under $XDG_CACHE_HOME (not /tmp, which may be memory) removed afterwards.
# Needs curl, xz, tar, sha256sum, python3 and Nix.
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
PIN=$ROOT/nix/clang-release.json

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

case ${1:-} in
  -h | --help | "")
    awk 'NR > 2 && /^#/ { sub(/^# ?/, ""); print; next } NR > 2 { exit }' "${BASH_SOURCE[0]}"
    [[ -n ${1:-} ]] && exit 0
    exit 2
    ;;
esac
version=$1
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "not a version: $version"
major=${version%%.*}

cache=${XDG_CACHE_HOME:-$HOME/.cache}
mkdir -p "$cache"
tmp=$(mktemp -d -p "$cache" brulion-clang-release.XXXXXX)
trap 'rm -rf -- "$tmp"' EXIT

# <system> <archive's name without .tar.xz>
pin_one() {
  local system=$1 archive=$2
  local url=https://github.com/llvm/llvm-project/releases/download/llvmorg-$version/$archive.tar.xz
  local dir=$tmp/$system
  mkdir -p "$dir/bin"
  echo "downloading $url" >&2
  curl --fail --location --silent --show-error --retry 3 "$url" |
    tee >(sha256sum | cut -d' ' -f1 >"$tmp/$system.sha256") |
    xz --decompress --stdout |
    tar -x -C "$dir/bin" --strip-components=2 "$archive/bin/clang-$major" ||
    die "cannot download and unpack $url"
  chmod 755 "$dir/bin/clang-$major"
  # (the digest is written by a process of its own: wait for it)
  local tries=0
  until [[ -s $tmp/$system.sha256 ]]; do
    tries=$((tries + 1))
    [[ $tries -le 50 ]] || die "no SHA-256 of $url"
    sleep 0.1
  done
  local sha256 hash
  sha256=$(<"$tmp/$system.sha256")
  hash=$(nix --extra-experimental-features nix-command hash path "$dir") || die "nix hash path failed"
  [[ $hash == sha256-* ]] || die "no Nix hash for $archive"
  rm -rf -- "$dir"
  python3 - "$PIN" "$version" "$system" "$archive" "$sha256" "$hash" <<'PY'
import json, sys
path, version, system, archive, sha256, nix_hash = sys.argv[1:]
with open(path, encoding="utf-8") as f:
    pin = json.load(f)
pin.setdefault("releases", {}).setdefault(version, {})[system] = {
    "archive": archive,
    "sha256": sha256,
    "hash": nix_hash,
}
with open(path, "w", encoding="utf-8") as f:
    json.dump(pin, f, indent=2, ensure_ascii=False)
    f.write("\n")
PY
  echo "nix/clang-release.json: Clang $version for $system, sha256 $sha256, $hash"
}

pin_one aarch64-linux "LLVM-$version-Linux-ARM64"
pin_one x86_64-linux "LLVM-$version-Linux-X64"
