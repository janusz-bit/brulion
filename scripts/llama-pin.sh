#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Pins another release of llama.cpp for the model on the device (3rdparty/llama/pin.json; see
# cmake/BrulionLocalAi.cmake): the tag, its source archive's SHA-256 (what scripts/llama-source.sh
# checks) and the Nix hash of the unpacked archive (what nix/package.nix and nix/android.nix
# fetch). Then build and run the tests with a model, on a computer and on a phone:
#   scripts/dev-build.sh -t tst_ai      (BRULION_LOCAL_MODEL=… for tst_ai_llama)
# and regenerate the prompts' goldens when the model's chat template changed
# (tests/core/ai/lfm3_goldens.py).
#
# Usage: scripts/llama-pin.sh <tag>          e.g. scripts/llama-pin.sh v0.9.0
# Needs curl, sha256sum, python3 and Nix (flakes).
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
PIN=$ROOT/3rdparty/llama/pin.json

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
tag=$1
[[ $tag =~ ^[A-Za-z0-9._-]+$ ]] || die "not a tag: $tag"
url=https://github.com/ggml-org/llama.cpp/archive/refs/tags/$tag.tar.gz

tmp=$(mktemp)
trap 'rm -f -- "$tmp"' EXIT
echo "downloading $url" >&2
curl --fail --location --silent --show-error --retry 3 -o "$tmp" "$url" || die "cannot download $url"
sha256=$(sha256sum "$tmp" | cut -d' ' -f1)
hash=$(nix --extra-experimental-features 'nix-command flakes' store prefetch-file --json --unpack "$url" |
  python3 -c 'import json, sys; print(json.load(sys.stdin)["hash"])') || die "nix store prefetch-file failed"
[[ $hash == sha256-* ]] || die "no Nix hash for $url"

python3 - "$PIN" "$tag" "$url" "$sha256" "$hash" <<'PY'
import json, sys
path, tag, url, sha256, nix_hash = sys.argv[1:]
with open(path, encoding="utf-8") as f:
    pin = json.load(f)
pin.update(tag=tag, url=url, sha256=sha256, hash=nix_hash)
with open(path, "w", encoding="utf-8") as f:
    json.dump(pin, f, indent=2, ensure_ascii=False)
    f.write("\n")
PY
echo "3rdparty/llama/pin.json: llama.cpp $tag, sha256 $sha256, $hash"
