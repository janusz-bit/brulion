#!/usr/bin/env bash
# scripts/store-manifests.py (packaging/STORES.md): the manifests of a made-up release from its
# SHA256SUMS — every store's file there, each naming the version and the right checksum (winget's in
# upper case), free text quoted for YAML, the same bytes on a second run; a checksum missing from
# the list or a version that is no release refused. No network.
set -uo pipefail
script=${1:?usage: tst_store_manifests.sh <store-manifests.py>}
command -v python3 >/dev/null || { echo "SKIP   : no python3"; exit 77; }
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
failures=0
check() { # description, command…
  local what=$1; shift
  if "$@" >/dev/null 2>&1; then echo "PASS   : $what"; else echo "FAIL!  : $what"; failures=$((failures + 1)); fi
}
v=9.8.7
sum() { printf '%064d' "$1"; }
{
  echo "$(sum 1)  brulion-$v-source.tar.gz"
  echo "$(sum 2)  Brulion-$v-windows-x86_64-setup.exe"
  echo "$(sum 3)  Brulion-$v-x86_64.AppImage"
  echo "$(sum 4)  Brulion-$v-aarch64.AppImage"
  echo "$(sum 5)  brulion-$v-arm64-v8a.apk"
} > "$work/SHA256SUMS.txt"

python3 -I "$script" --version $v --sums "$work/SHA256SUMS.txt" --out "$work/a" >/dev/null 2>&1
python3 -I "$script" --version $v --sums "$work/SHA256SUMS.txt" --out "$work/b" >/dev/null 2>&1
w=$work/a/winget/manifests/j/janusz-bit/Brulion/$v
check "every store's files are written" test -s "$work/a/flathub/io.github.januszbit.brulion.yml" -a -s "$w/janusz-bit.Brulion.yaml" \
  -a -s "$w/janusz-bit.Brulion.installer.yaml" -a -s "$w/janusz-bit.Brulion.locale.en-US.yaml" \
  -a -s "$w/janusz-bit.Brulion.locale.pl-PL.yaml" -a -s "$work/a/aur/brulion-bin/PKGBUILD" \
  -a -s "$work/a/aur/brulion-bin/.SRCINFO" -a -s "$work/a/homebrew/brulion.rb"
check "the same input gives the same bytes" diff -r "$work/a" "$work/b"
check "Flathub builds the source tarball of the version" grep -q "sha256: $(sum 1)" "$work/a/flathub/io.github.januszbit.brulion.yml"
check "… from its release" grep -q "releases/download/v$v/brulion-$v-source.tar.gz" "$work/a/flathub/io.github.januszbit.brulion.yml"
check "winget installs the setup, its checksum in upper case" grep -q "InstallerSha256: $(sum 2 | tr a-f A-F)" "$w/janusz-bit.Brulion.installer.yaml"
check "… the version in every manifest" test "$(grep -l "PackageVersion: $v" "$w"/*.yaml | wc -l)" -eq 4
check "… free text quoted (a colon stays text)" grep -q "^ShortDescription: '" "$w/janusz-bit.Brulion.locale.pl-PL.yaml"
check "the AUR package takes both AppImages" grep -q "sha256sums_x86_64=('$(sum 3)')" "$work/a/aur/brulion-bin/PKGBUILD"
check "… its .SRCINFO agrees" grep -q "sha256sums_aarch64 = $(sum 4)" "$work/a/aur/brulion-bin/.SRCINFO"
check "Homebrew builds the source tarball" grep -q "sha256 \"$(sum 1)\"" "$work/a/homebrew/brulion.rb"
check "nothing left unformatted" bash -c "! grep -rq '{[A-Za-z_]*}' '$work/a/winget' '$work/a/flathub'"

grep -v "AppImage" "$work/SHA256SUMS.txt" | grep -v aarch64 > "$work/short.txt"
check "a file missing from the checksums is refused" bash -c "! python3 -I '$script' --version $v --sums '$work/short.txt' --out '$work/c'"
check "a version that is no release is refused" bash -c "! python3 -I '$script' --version 1.0-rc1 --sums '$work/SHA256SUMS.txt' --out '$work/d'"

echo "Totals: $failures failed"
exit $((failures > 0))
