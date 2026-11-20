#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The sources of llama.cpp for the model on the device (cmake/BrulionLocalAi.cmake,
# 3rdparty/llama): downloads the release pinned in 3rdparty/llama/pin.json, checks the archive's
# SHA-256, unpacks it and prints the folder — what -DBRULION_LLAMA_SOURCE_DIR takes. With
# --whisper the same for whisper.cpp, speech recognition over llama.cpp's ggml
# (cmake/BrulionSpeech.cmake, 3rdparty/whisper/pin.json): -DBRULION_WHISPER_SOURCE_DIR. Run again,
# it only prints the folder (an unpacked release is kept). The Nix builds do not use it (they fetch
# the same release by its Nix hash).
#
# Usage: scripts/llama-source.sh [--whisper] [--help]
#
# Environment (optional):
#   BRULION_LLAMA_DIR   where releases are kept (default ~/.cache/brulion-llama): <tag>/ and the
#                       archive <tag>.tar.gz (whisper.cpp's: whisper-<tag>/, whisper-<tag>.tar.gz)
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
PIN=$ROOT/3rdparty/llama/pin.json
NAME=llama.cpp
HEADER=include/llama.h
PREFIX=

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

case ${1:-} in
  -h | --help)
    awk 'NR > 2 && /^#/ { sub(/^# ?/, ""); print; next } NR > 2 { exit }' "${BASH_SOURCE[0]}"
    exit 0
    ;;
  --whisper)
    PIN=$ROOT/3rdparty/whisper/pin.json
    NAME=whisper.cpp
    HEADER=include/whisper.h
    PREFIX=whisper-
    ;;
  "") ;;
  *) die "unknown argument: $1 (see --help)" ;;
esac

# A value of the pin (a flat JSON object of strings and numbers).
pin() {
  sed -nE "s/^[[:space:]]*\"$1\"[[:space:]]*:[[:space:]]*\"?([^\",]*)\"?,?[[:space:]]*\$/\1/p" "$PIN" | head -n 1
}
tag=$(pin tag)
url=$(pin url)
sha256=$(pin sha256)
[[ -n $tag && -n $url && ${#sha256} == 64 ]] || die "cannot read tag, url and sha256 from $PIN"

dir=${BRULION_LLAMA_DIR:-$HOME/.cache/brulion-llama}
src=$dir/$PREFIX$tag
archive=$dir/$PREFIX$tag.tar.gz
if [[ -f $src/$HEADER && -f $src/.brulion-sha256 && $(<"$src/.brulion-sha256") == "$sha256" ]]; then
  echo "$src"
  exit 0
fi

mkdir -p "$dir"
if [[ ! -f $archive ]] || ! echo "$sha256  $archive" | sha256sum --check --status; then
  echo "downloading $NAME $tag: $url" >&2
  curl --fail --location --silent --show-error --retry 3 -o "$archive.part" "$url" || die "cannot download $url"
  mv -- "$archive.part" "$archive"
fi
echo "$sha256  $archive" | sha256sum --check --status ||
  die "$archive: SHA-256 differs from ${PIN#"$ROOT"/} ($sha256)"

rm -rf -- "$src.part" "$src"
mkdir -p "$src.part"
tar -xzf "$archive" -C "$src.part" --strip-components=1
[[ -f $src.part/$HEADER ]] || die "$archive: no $HEADER inside"
echo "$sha256" >"$src.part/.brulion-sha256"
mv -- "$src.part" "$src"
echo "$src"
