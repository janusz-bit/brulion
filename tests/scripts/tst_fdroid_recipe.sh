#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Checks the draft F-Droid recipe (packaging/fdroid/io.github.januszbit.brulion.yml) against what
# it calls: the versions pinned by the build scripts (NDK, SDK packages, Qt and OpenSSL srclibs, Qt
# modules), the options it sets, the APK path android-build.sh writes and the versionCode formula
# of src/app/CMakeLists.txt. No network, no build.
#
# Usage: tests/scripts/tst_fdroid_recipe.sh [repository root]   (default: two levels up)
#
# The recipe's and the scripts' texts are matched literally ($$SDK$$, $OUT_DIR…).
# shellcheck disable=SC2016
set -euo pipefail

root=$(realpath -- "${1:-$(dirname -- "${BASH_SOURCE[0]}")/../..}")
recipe=$root/packaging/fdroid/io.github.januszbit.brulion.yml
build_sh=$root/scripts/android-build.sh
qt_sh=$root/scripts/android-qt-from-source.sh
ssl_sh=$root/scripts/android-openssl-from-source.sh
for f in "$recipe" "$build_sh" "$qt_sh" "$ssl_sh" "$root/CMakeLists.txt"; do
  [[ -f $f ]] || {
    echo "no $f" >&2
    exit 2
  }
done

failed=0
pass() { printf 'PASS   : %s\n' "$1"; }
fail() {
  printf 'FAIL!  : %s\n' "$1"
  failed=$((failed + 1))
}
same() { # <description> <expected> <actual>
  if [[ $2 == "$3" ]]; then pass "$1 ($3)"; else fail "$1: expected '$2', got '$3'"; fi
}
contains() { # <description> <file> <fixed text>
  if grep -qF -- "$3" "$2"; then pass "$1"; else fail "$1: '$3' not in ${2#"$root"/}"; fi
}

text=$(<"$recipe")
field() { sed -nE "s/^$1: *(.*)$/\\1/p" <<<"$text" | head -n 1; }
# Fields of the last (newest) build entry: "    key: value" after the last "  - versionName:".
last_build=$(awk '/^  - versionName:/ { buf = "" } { buf = buf $0 "\n" } END { printf "%s", buf }' <<<"$text")
build_field() { sed -nE "s/^ *(- )?$1: *(.*)$/\\2/p" <<<"$last_build" | head -n 1; }

# --- metadata --------------------------------------------------------------------------------------

same "License" AGPL-3.0-or-later "$(field License)"
same "RepoType" git "$(field RepoType)"
same "Repo" https://github.com/janusz-bit/brulion.git "$(field Repo)"
same "SourceCode" https://github.com/janusz-bit/brulion "$(field SourceCode)"
same "AutoUpdateMode" Version "$(field AutoUpdateMode)"
case $(field UpdateCheckMode) in
  Tags*) pass "UpdateCheckMode: Tags" ;;
  *) fail "UpdateCheckMode should be Tags, not '$(field UpdateCheckMode)'" ;;
esac
if [[ $text == *$'\nCategories:\n  - '* ]]; then pass "Categories"; else fail "no Categories"; fi

# --- version ---------------------------------------------------------------------------------------

cmake_version=$(sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$root/CMakeLists.txt" | head -n 1)
name=$(build_field versionName)
code=$(build_field versionCode)
if [[ $name =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
  # src/app/CMakeLists.txt: major * 1000000 + minor * 1000 + patch
  same "versionCode of $name (major*1000000 + minor*1000 + patch)" \
    $((BASH_REMATCH[1] * 1000000 + BASH_REMATCH[2] * 1000 + BASH_REMATCH[3])) "$code"
else
  fail "versionName '$name' is not X.Y.Z"
fi
contains "the versionCode formula of src/app/CMakeLists.txt" "$root/src/app/CMakeLists.txt" \
  '${PROJECT_VERSION_MAJOR} * 1000000 + ${PROJECT_VERSION_MINOR} * 1000 + ${PROJECT_VERSION_PATCH}'
# The newest build is not ahead of the sources (F-Droid's bot adds later ones to fdroiddata).
if [[ $(printf '%s\n%s\n' "$name" "$cmake_version" | sort -V | tail -n 1) == "$cmake_version" ]]; then
  pass "newest build $name <= CMakeLists.txt $cmake_version"
else
  fail "newest build $name is newer than CMakeLists.txt $cmake_version"
fi
same "commit is the tag" "v$name" "$(build_field commit)"
same "CurrentVersion" "$name" "$(field CurrentVersion)"
same "CurrentVersionCode" "$code" "$(field CurrentVersionCode)"

# --- toolchain pins ----------------------------------------------------------------------------------

ndk_version=$(sed -nE 's/^NDK_VERSION=\$\{NDK_VERSION:-([0-9.]+)\}$/\1/p' "$build_sh")
same "android-build.sh pins NDK r27c" 27.2.12479018 "$ndk_version"
same "ndk" r27c "$(build_field ndk)"
for script in "$qt_sh" "$ssl_sh"; do
  contains "${script#"$root"/} defaults to the same NDK" "$script" "NDK_VERSION=\${NDK_VERSION:-$ndk_version}"
done
platform=$(sed -nE 's/^SDK_PLATFORM=(.*)$/\1/p' "$build_sh")
build_tools=$(sed -nE 's/^BUILD_TOOLS=(.*)$/\1/p' "$build_sh")
sdk_cmake=$(sed -nE 's/^SDK_CMAKE=(.*)$/\1/p' "$build_sh")
contains "sdkmanager installs the pinned SDK packages" "$recipe" \
  "sdkmanager 'platforms;$platform' 'build-tools;$build_tools' 'cmake;$sdk_cmake'"
contains "the SDK's CMake on PATH" "$recipe" "\$\$SDK\$\$/cmake/$sdk_cmake/bin"
same "android-qt-from-source.sh uses the same SDK CMake" "$sdk_cmake" "$(sed -nE 's/^SDK_CMAKE=(.*)$/\1/p' "$qt_sh")"

qt_version=$(sed -nE 's/^QT_VERSION=([0-9.]+)$/\1/p' "$qt_sh")
same "android-build.sh's default Qt is the one built from source" "$qt_version" \
  "$(sed -nE 's/^QT_VERSION=\$\{QT_VERSION:-([0-9.]+)\}$/\1/p' "$build_sh")"
contains "srclib Qt5 at v$qt_version" "$recipe" "- Qt5@v$qt_version"
modules=$(sed -nE 's/^HOST_MODULES=\((.*)\)$/\1/p' "$qt_sh" | tr ' ' ',')
contains "init-repository fetches exactly the modules the script builds ($modules)" "$recipe" \
  "./init-repository -f --module-subset=$modules"
ssl_version=$(sed -nE 's/^OPENSSL_VERSION=([0-9.]+)$/\1/p' "$ssl_sh")
contains "srclib OpenSSL at openssl-$ssl_version" "$recipe" "- OpenSSL@openssl-$ssl_version"

# --- build commands ----------------------------------------------------------------------------------

contains "builds with scripts/android-build.sh" "$recipe" "- scripts/android-build.sh"
for setting in BRULION_ANDROID_QT=source BRULION_ANDROID_SIGN=0 BRULION_UPDATE_CHECK=OFF \
  'BRULION_QT_SOURCE_DIR="$$Qt5$$"' 'BRULION_OPENSSL_SOURCE_DIR="$$OpenSSL$$"'; do
  contains "sets $setting" "$recipe" "$setting"
done
# Every BRULION_* variable of the recipe is one the scripts read (BRULION_UPDATE_CHECK: when the
# CMake option exists, android-build.sh must forward it).
while read -r var; do
  if [[ $var == BRULION_UPDATE_CHECK ]] && ! grep -rqs 'BRULION_UPDATE_CHECK' "$root/CMakeLists.txt" "$root/src" "$root/cmake"; then
    pass "$var (no such CMake option in this tree yet)"
    continue
  fi
  if grep -qF -- "$var" "$build_sh" "$qt_sh" "$ssl_sh"; then
    pass "$var is read by the scripts"
  else
    fail "$var is set by the recipe but no build script reads it"
  fi
done < <(grep -oE 'BRULION_[A-Z0-9_]+' "$recipe" | sort -u)
contains "android-build.sh writes dist-android/" "$build_sh" "OUT_DIR=dist-android"
contains "android-build.sh names the unsigned APK" "$build_sh" 'apk=$OUT_DIR/brulion-$VERSION-$abi_tag$unsigned_suffix.apk'
contains "android-build.sh's unsigned suffix" "$build_sh" "unsigned_suffix=-unsigned"
same "output" 'dist-android/brulion-$$VERSION$$-arm64-v8a-unsigned.apk' "$(build_field output)"
same "android-build.sh's default ABI is the recipe's" arm64-v8a \
  "$(sed -nE 's/.*\$\{BRULION_ANDROID_ABIS:-([a-z0-9_-]+)\}.*/\1/p' "$build_sh" | head -n 1)"
if grep -qE '^ *- .*(aqt|android_openssl|KDAB)' <<<"$last_build"; then
  fail "the build fetches prebuilt Qt or OpenSSL"
else
  pass "no prebuilt Qt (aqt) or OpenSSL (KDAB) in the build"
fi

echo "Totals: $((failed)) failed"
((failed == 0))
