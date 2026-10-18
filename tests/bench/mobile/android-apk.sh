#!/usr/bin/env bash
# android-apk.sh <source-tree> <tag> <abi> [rel|dbg] — one APK variant of a source tree for the
# mobile benchmarks (x86_64 Linux host: the laptop). Wraps scripts/android-build.sh:
#   rel  the APK as shipped: Release, signed with the THROWAWAY key (the release keystore
#        variables are unset on purpose — benchmarks never touch the real key)
#   dbg  a measurement copy (android-probe/measure-copy.sh: debuggable + the bench probe),
#        also Release-optimised, same throwaway key (so it installs over `rel` keeping the data)
# Output: $W/apk/<tag>.apk, the unstripped main library $W/apk/<tag>.libbrulion.so, the log
# $W/logs/build-<tag>.log; build dir $W/build-<tag>. W = $BENCH_MOBILE_DIR
# (default ~/work/brulion-perf-mobile). JOBS (default 8). Two builds of the same source tree must
# not run at the same time (android-build.sh writes dist-android/ inside the tree).
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
W=${BENCH_MOBILE_DIR:-$HOME/work/brulion-perf-mobile}
src=$(cd "$1" && pwd)
tag=$2
abi=$3
variant=${4:-rel}
mkdir -p "$W/apk" "$W/logs"
unset QT_ANDROID_KEYSTORE_PATH QT_ANDROID_KEYSTORE_BASE64 QT_ANDROID_KEYSTORE_ALIAS \
  QT_ANDROID_KEYSTORE_STORE_PASS QT_ANDROID_KEYSTORE_KEY_PASS
tree=$src
case $variant in
  rel) ;;
  dbg)
    tree=$W/src-$tag
    "$here/android-probe/measure-copy.sh" "$src" "$tree"
    ;;
  *) echo "android-apk: unknown variant $variant" >&2; exit 2 ;;
esac
cd "$tree"
export BRULION_ANDROID_ABIS=$abi BRULION_ANDROID_BUILD_TYPE=Release BRULION_ANDROID_BUILD_DIR=$W/build-$tag
export JOBS=${JOBS:-8}
log=$W/logs/build-$tag.log
start=$(date +%s)
if ! scripts/android-build.sh >"$log" 2>&1; then
  echo "android-apk: BUILD FAILED $tag (see $log)"
  tail -40 "$log"
  exit 1
fi
cp "dist-android/brulion-$(sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt | head -n 1)-$abi.apk" "$W/apk/$tag.apk"
lib=$(find "$W/build-$tag/src/app" -maxdepth 1 -name "libbrulion_$abi.so" | head -n 1)
[[ -n $lib ]] && cp "$lib" "$W/apk/$tag.libbrulion.so"
echo "android-apk: $tag ($variant, $abi) in $(($(date +%s) - start)) s: $(stat -c %s "$W/apk/$tag.apk") B"
grep -E "Signer #1 certificate DN|^APK:" "$log" | tail -2
