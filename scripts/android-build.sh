#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Builds Brulion's Android APK (optionally also an AAB) with Qt for Android: installs what is
# missing (Qt via aqt, NDK/platform/build-tools via sdkmanager), configures with CMake, builds
# the `brulion` target and its APK with androiddeployqt (Gradle), signs the APK and checks it.
# Idempotent: run it again after changes (an existing build directory is reused).
# Works on a normal x86_64 Linux (GitHub Actions) and on NixOS (needs programs.nix-ld; the
# script re-runs itself inside `nix shell` with ccache and JDK 21). CMake and Ninja come from
# the Android SDK (package cmake;3.31.6) unless BRULION_ANDROID_CMAKE=system.
#
# Usage: scripts/android-build.sh [--help]
#
# Environment (all optional):
#   QT_VERSION                   Qt version (default 6.11.2)
#   QT_ROOT                      where aqt installs Qt (default ~/.cache/brulion-qt):
#                                $QT_ROOT/$QT_VERSION/android_<abi> and .../gcc_64 (host)
#   QT_HOST_PATH                 host Qt of the same version (default $QT_ROOT/$QT_VERSION/gcc_64)
#   ANDROID_SDK_ROOT             Android SDK (else ANDROID_HOME, else ~/.cache/brulion-android-sdk);
#                                missing packages are installed with its cmdline-tools (sdkmanager)
#   ANDROID_NDK_ROOT             NDK (default $ANDROID_SDK_ROOT/ndk/$NDK_VERSION)
#   NDK_VERSION                  default 27.2.12479018 (r27c, the NDK of Qt 6.11)
#   JAVA_HOME                    JDK 21 (17+ works); default: the `java` on PATH
#   BRULION_ANDROID_ABIS         ABIs, space separated (default "arm64-v8a"; also armeabi-v7a,
#                                x86_64, x86 — one multi-ABI APK)
#   BRULION_ANDROID_BUILD_DIR    build directory (default build-android)
#   BRULION_ANDROID_BUILD_TYPE   Release (default), RelWithDebInfo or Debug
#   BRULION_ANDROID_AAB=1        also build an Android App Bundle (.aab)
#   BRULION_ANDROID_UNIT_TESTS=1 also run the Kotlin unit tests (android/tests)
#   BRULION_ANDROID_LINT=1       also run Android lint and list the issues in Brulion's files
#   BRULION_ANDROID_SELFTEST=1   also build the bridge self-test APK (same package as the app!)
#                                for scripts/android-emulator-test.sh
#   BRULION_ANDROID_VERSION_CODE versionCode (default major*1000000 + minor*1000 + patch)
#   BRULION_ANDROID_CMAKE=system use the cmake/ninja on PATH instead of the SDK's (NixOS's
#                                patched cmake does not find the NDK sysroot's EGL/GLES headers)
#   BRULION_ANDROID_OPENSSL_DIR  a checkout of KDAB/android_openssl for offline builds (default:
#                                CMake downloads the pinned OpenSSL 3 libraries, src/app/CMakeLists.txt)
#   BRULION_WERROR=ON            warnings in Brulion's own C++ code are errors
#   JOBS                         parallel compile jobs (default: all cores)
#   SOURCE_DATE_EPOCH            time stamp of the files in Qt resources (rcc), for reproducible
#                                builds (default: the time of the last git commit)
# Signing — the release keystore (same variables androiddeployqt reads):
#   QT_ANDROID_KEYSTORE_PATH     keystore file, or
#   QT_ANDROID_KEYSTORE_BASE64   the keystore as base64 (CI secret)
#   QT_ANDROID_KEYSTORE_ALIAS    key alias
#   QT_ANDROID_KEYSTORE_STORE_PASS, QT_ANDROID_KEYSTORE_KEY_PASS (default: the store password)
#   Without a keystore the APK is signed with a THROWAWAY key kept in
#   ~/.cache/brulion-android/ (BRULION_THROWAWAY_KEYSTORE): it installs, but cannot update an
#   APK signed with the release key. Create a release key: scripts/android-new-keystore.sh.
#
# Output: dist-android/brulion-<version>-<abis>.apk (+ .aab). On GitHub Actions also written to
# $GITHUB_OUTPUT as `apk`, `aab`, `version`, `signing` (release/throwaway) and `openssl` (its
# version).
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
SCRIPT="$ROOT/scripts/$(basename -- "${BASH_SOURCE[0]}")"
cd "$ROOT"

info() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
warn() {
  # $1 = title, $2 = details (a GitHub annotation in CI)
  if [[ -n ${GITHUB_ACTIONS:-} ]]; then echo "::warning title=$1::$2"; fi
  printf '\n\033[1;33m!!! WARNING: %s\033[0m\n\033[33m!!! %s\033[0m\n\n' "$1" "$2" >&2
}
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

[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] ||
  die "needs an x86_64 Linux host (the NDK and Qt's host tools are x86_64 only); on the Pi use laptop-run"

# --- NixOS: tools from nixpkgs, libraries of the prebuilt binaries via nix-ld ----------------

if [[ -e /etc/NIXOS && -z ${BRULION_ANDROID_IN_NIX_SHELL:-} ]]; then
  [[ -e /lib64/ld-linux-x86-64.so.2 ]] ||
    die "NixOS without nix-ld: the NDK, aqt's Qt host tools and Gradle's aapt2 are prebuilt binaries. Enable programs.nix-ld (or run this script under steam-run)."
  info "NixOS: entering nix shell (ccache, JDK 21) with nix-ld libraries for Qt's host tools"
  # Qt's prebuilt host tools (moc, rcc, qmlcachegen, androiddeployqt, ...) link glib, krb5,
  # brotli: add them to nix-ld's search path for this build only.
  libs=$(nix build --no-link --print-out-paths nixpkgs#glib.out nixpkgs#krb5.lib nixpkgs#brotli.lib nixpkgs#keyutils.lib) ||
    die "nix build of the host-tool libraries failed"
  ld_path=
  for p in $libs; do ld_path+="$p/lib:"; done
  export NIX_LD_LIBRARY_PATH="${ld_path}${NIX_LD_LIBRARY_PATH:-/run/current-system/sw/share/nix-ld/lib}"
  export BRULION_ANDROID_IN_NIX_SHELL=1
  exec nix shell nixpkgs#ccache nixpkgs#jdk21_headless nixpkgs#unzip --command bash "$SCRIPT" "$@"
fi

# --- configuration ---------------------------------------------------------------------------

QT_VERSION=${QT_VERSION:-6.11.2}
QT_ROOT=${QT_ROOT:-$HOME/.cache/brulion-qt}
NDK_VERSION=${NDK_VERSION:-27.2.12479018}
SDK_PLATFORM=android-36
BUILD_TOOLS=36.0.0
SDK_CMAKE=3.31.6
AQT_VERSION=3.3.0
read -r -a ABIS <<<"${BRULION_ANDROID_ABIS:-arm64-v8a}"
((${#ABIS[@]})) || die "BRULION_ANDROID_ABIS is empty"
BUILD_DIR=${BRULION_ANDROID_BUILD_DIR:-build-android}
BUILD_TYPE=${BRULION_ANDROID_BUILD_TYPE:-Release}
JOBS=${JOBS:-$(nproc)}
OUT_DIR=dist-android
VERSION=$(sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt | head -n 1)
[[ -n $VERSION ]] || die "cannot read the project version from CMakeLists.txt"

qt_arch() { # ABI -> aqt arch / Qt directory name
  case $1 in
    arm64-v8a) echo android_arm64_v8a ;;
    armeabi-v7a) echo android_armv7 ;;
    x86_64) echo android_x86_64 ;;
    x86) echo android_x86 ;;
    *) die "unknown Android ABI: $1" ;;
  esac
}

info "Brulion $VERSION for Android: Qt $QT_VERSION, ABIs ${ABIS[*]}, $BUILD_TYPE"

# --- JDK ----------------------------------------------------------------------------------------

if [[ -z ${JAVA_HOME:-} ]]; then
  java=$(command -v java || true)
  [[ -n $java ]] || die "no JDK: install JDK 21 (or 17+) or set JAVA_HOME"
  JAVA_HOME=$(dirname -- "$(dirname -- "$(readlink -f -- "$java")")")
fi
export JAVA_HOME
[[ -x $JAVA_HOME/bin/java ]] || die "JAVA_HOME=$JAVA_HOME has no bin/java"
export PATH="$JAVA_HOME/bin:$PATH"
java_major=$("$JAVA_HOME/bin/java" -XshowSettings:properties -version 2>&1 |
  sed -nE 's/^[[:space:]]*java\.specification\.version = ([0-9]+).*/\1/p')
((${java_major:-0} >= 17)) || die "JDK ${java_major:-?} is too old: Gradle 9 / AGP 9 need 17+, Qt 6.11 recommends 21"
echo "JDK $java_major: $JAVA_HOME"

# --- Android SDK and NDK ------------------------------------------------------------------------

SDK=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/.cache/brulion-android-sdk}}
mkdir -p "$SDK"
SDK=$(cd -- "$SDK" && pwd)
NDK=${ANDROID_NDK_ROOT:-$SDK/ndk/$NDK_VERSION}

sdk_packages=()
[[ -f $SDK/platforms/$SDK_PLATFORM/android.jar ]] || sdk_packages+=("platforms;$SDK_PLATFORM")
[[ -x $SDK/build-tools/$BUILD_TOOLS/aapt2 ]] || sdk_packages+=("build-tools;$BUILD_TOOLS")
[[ -f $NDK/source.properties ]] || sdk_packages+=("ndk;$NDK_VERSION")
use_sdk_cmake=1
[[ ${BRULION_ANDROID_CMAKE:-sdk} == system ]] && use_sdk_cmake=0
if ((use_sdk_cmake)) && [[ ! -x $SDK/cmake/$SDK_CMAKE/bin/cmake ]]; then
  sdk_packages+=("cmake;$SDK_CMAKE")
fi
if ((${#sdk_packages[@]})); then
  sdkmanager=
  for candidate in "$SDK/cmdline-tools/latest/bin/sdkmanager" "$(command -v sdkmanager || true)"; do
    if [[ -n $candidate && -f $candidate ]]; then
      sdkmanager=$candidate
      break
    fi
  done
  [[ -n $sdkmanager ]] ||
    die "$SDK lacks ${sdk_packages[*]} and has no cmdline-tools: install them (Android Studio > SDK Manager, or https://developer.android.com/studio#command-line-tools-only into $SDK/cmdline-tools/latest)"
  info "Installing Android SDK packages: ${sdk_packages[*]}"
  yes 2>/dev/null | sh "$sdkmanager" --sdk_root="$SDK" --licenses >/dev/null || true
  sh "$sdkmanager" --sdk_root="$SDK" --install "${sdk_packages[@]}" | grep -v '^\[' || true
  [[ -f $NDK/source.properties ]] || NDK=$SDK/ndk/$NDK_VERSION
  [[ -f $SDK/platforms/$SDK_PLATFORM/android.jar ]] || die "sdkmanager did not install platforms;$SDK_PLATFORM"
  [[ -x $SDK/build-tools/$BUILD_TOOLS/aapt2 ]] || die "sdkmanager did not install build-tools;$BUILD_TOOLS"
fi
[[ -f $NDK/source.properties ]] || die "Android NDK not found at $NDK"
ndk_revision=$(sed -nE 's/^Pkg\.Revision *= *([0-9.]+).*/\1/p' "$NDK/source.properties")
[[ $ndk_revision == "$NDK_VERSION" ]] ||
  warn "NDK $ndk_revision instead of $NDK_VERSION" "Qt $QT_VERSION is built and tested with NDK $NDK_VERSION"
BT=$SDK/build-tools/$BUILD_TOOLS
export ANDROID_SDK_ROOT=$SDK ANDROID_HOME=$SDK ANDROID_NDK_ROOT=$NDK
if ((use_sdk_cmake)); then
  [[ -x $SDK/cmake/$SDK_CMAKE/bin/cmake ]] || die "sdkmanager did not install cmake;$SDK_CMAKE"
  export PATH="$SDK/cmake/$SDK_CMAKE/bin:$PATH"
fi
{ command -v cmake && command -v ninja; } >/dev/null || die "cmake and ninja are needed"
echo "SDK: $SDK"
echo "NDK: $NDK ($ndk_revision)"
echo "CMake: $(command -v cmake) ($(cmake --version | head -n 1 | cut -d' ' -f3)), Ninja $(ninja --version)"

# --- Qt for Android (aqt) -----------------------------------------------------------------------

aqt() {
  if command -v uvx >/dev/null; then
    uvx --from "aqtinstall==$AQT_VERSION" aqt "$@"
  elif command -v pipx >/dev/null; then
    pipx run --spec "aqtinstall==$AQT_VERSION" aqt "$@"
  else
    local venv=$QT_ROOT/.aqt-venv
    [[ -x $venv/bin/aqt ]] || {
      python3 -m venv "$venv" && "$venv/bin/pip" install --quiet "aqtinstall==$AQT_VERSION"
    } || die "cannot install aqtinstall (install uv, pipx or python3-venv)"
    "$venv/bin/aqt" "$@"
  fi
}

QT_HOST_PATH=${QT_HOST_PATH:-$QT_ROOT/$QT_VERSION/gcc_64}
for abi in "${ABIS[@]}"; do
  arch=$(qt_arch "$abi")
  if [[ ! -f $QT_ROOT/$QT_VERSION/$arch/lib/cmake/Qt6/qt.toolchain.cmake ]]; then
    info "Installing Qt $QT_VERSION $arch (aqt) into $QT_ROOT"
    mkdir -p "$QT_ROOT"
    aqt install-qt all_os android "$QT_VERSION" "$arch" -O "$QT_ROOT"
  fi
done
if [[ ! -x $QT_HOST_PATH/libexec/moc ]]; then
  [[ $QT_HOST_PATH == "$QT_ROOT/$QT_VERSION/gcc_64" ]] || die "QT_HOST_PATH=$QT_HOST_PATH has no libexec/moc"
  info "Installing the host Qt $QT_VERSION linux_gcc_64 (aqt) into $QT_ROOT"
  aqt install-qt linux desktop "$QT_VERSION" linux_gcc_64 -O "$QT_ROOT"
fi
# Fail early and readably when a prebuilt host tool cannot start (missing shared libraries).
for tool in libexec/moc libexec/rcc libexec/qmlcachegen libexec/qmlimportscanner bin/androiddeployqt; do
  out=$("$QT_HOST_PATH/$tool" --help 2>&1 || true)
  if grep -q 'error while loading shared libraries' <<<"$out"; then
    die "Qt host tool $QT_HOST_PATH/$tool does not start: $(head -n 1 <<<"$out")"
  fi
done
QT_MAIN=$QT_ROOT/$QT_VERSION/$(qt_arch "${ABIS[0]}")
echo "Qt: $QT_MAIN (host tools: $QT_HOST_PATH)"

# --- signing key -----------------------------------------------------------------------------

cleanup_files=()
trap 'rm -rf -- "${cleanup_files[@]}"' EXIT

signing=release
if [[ -n ${QT_ANDROID_KEYSTORE_BASE64:-} ]]; then
  keystore=${RUNNER_TEMP:-${TMPDIR:-/tmp}}/brulion-release-$$.jks
  cleanup_files+=("$keystore")
  (
    umask 077
    printf '%s' "$QT_ANDROID_KEYSTORE_BASE64" | tr -d ' \t\r\n' | base64 --decode >"$keystore"
  ) || die "QT_ANDROID_KEYSTORE_BASE64 is not valid base64"
  export QT_ANDROID_KEYSTORE_PATH=$keystore
fi
if [[ -n ${QT_ANDROID_KEYSTORE_PATH:-} ]]; then
  [[ -f $QT_ANDROID_KEYSTORE_PATH ]] || die "QT_ANDROID_KEYSTORE_PATH=$QT_ANDROID_KEYSTORE_PATH does not exist"
  [[ -n ${QT_ANDROID_KEYSTORE_ALIAS:-} ]] || die "QT_ANDROID_KEYSTORE_ALIAS is required with a release keystore"
  [[ -n ${QT_ANDROID_KEYSTORE_STORE_PASS:-} ]] || die "QT_ANDROID_KEYSTORE_STORE_PASS is required with a release keystore"
  export QT_ANDROID_KEYSTORE_KEY_PASS=${QT_ANDROID_KEYSTORE_KEY_PASS:-$QT_ANDROID_KEYSTORE_STORE_PASS}
  # Fail before the long build on a wrong password or alias.
  if ! out=$(keytool -list -keystore "$QT_ANDROID_KEYSTORE_PATH" -storepass:env QT_ANDROID_KEYSTORE_STORE_PASS \
    -alias "$QT_ANDROID_KEYSTORE_ALIAS" 2>&1); then
    printf '%s\n' "$out" >&2
    die "cannot open key '$QT_ANDROID_KEYSTORE_ALIAS' in $QT_ANDROID_KEYSTORE_PATH (wrong password or alias?)"
  fi
else
  signing=throwaway
  throwaway=${BRULION_THROWAWAY_KEYSTORE:-${XDG_CACHE_HOME:-$HOME/.cache}/brulion-android/throwaway.jks}
  if [[ ! -f $throwaway || ! -f $throwaway.pass ]]; then
    info "No release keystore: creating a throwaway signing key ($throwaway)"
    mkdir -p -- "$(dirname -- "$throwaway")"
    rm -f -- "$throwaway" "$throwaway.pass"
    (
      umask 077
      od -An -tx1 -N24 /dev/urandom | tr -d ' \n' >"$throwaway.pass"
    )
    QT_ANDROID_KEYSTORE_STORE_PASS=$(<"$throwaway.pass") keytool -genkeypair -noprompt \
      -keystore "$throwaway" -storetype PKCS12 -alias brulion-throwaway \
      -storepass:env QT_ANDROID_KEYSTORE_STORE_PASS -keypass:env QT_ANDROID_KEYSTORE_STORE_PASS \
      -keyalg RSA -keysize 2048 -validity 10000 \
      -dname "CN=Brulion unofficial build, O=Throwaway key" >/dev/null 2>&1 ||
      die "keytool could not create $throwaway"
  fi
  export QT_ANDROID_KEYSTORE_PATH=$throwaway QT_ANDROID_KEYSTORE_ALIAS=brulion-throwaway
  QT_ANDROID_KEYSTORE_STORE_PASS=$(<"$throwaway.pass")
  export QT_ANDROID_KEYSTORE_STORE_PASS QT_ANDROID_KEYSTORE_KEY_PASS=$QT_ANDROID_KEYSTORE_STORE_PASS
  warn "APK signed with a THROWAWAY key ($throwaway)" \
    "No release keystore (QT_ANDROID_KEYSTORE_PATH or _BASE64, _ALIAS, _STORE_PASS) was given. The APK installs, but it cannot update a Brulion APK signed with the release key (uninstalling first deletes app data). See scripts/android-new-keystore.sh."
fi

# --- configure + build -------------------------------------------------------------------------

# Reproducible APKs: rcc stamps the files it compiles into the library with this time instead of
# their modification times (which differ between checkouts). The Nix build uses 315532800.
if [[ -z ${SOURCE_DATE_EPOCH:-} ]] && epoch=$(git -C "$ROOT" log -1 --format=%ct 2>/dev/null) && [[ -n $epoch ]]; then
  export SOURCE_DATE_EPOCH=$epoch
fi
echo "SOURCE_DATE_EPOCH: ${SOURCE_DATE_EPOCH:-not set (no git checkout): file times}"

abi_list=$(IFS=';' && echo "${ABIS[*]}")
cmake_args=(
  -S "$ROOT" -B "$BUILD_DIR" -G Ninja
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
  -DCMAKE_TOOLCHAIN_FILE="$QT_MAIN/lib/cmake/Qt6/qt.toolchain.cmake"
  -DQT_HOST_PATH="$QT_HOST_PATH"
  -DANDROID_SDK_ROOT="$SDK"
  -DANDROID_NDK_ROOT="$NDK"
  -DQT_ANDROID_ABIS="$abi_list"
  -DBUILD_TESTING=OFF
  # 16 KB page sizes (Android 15+ devices): NDK r27 links 64-bit libraries with 16 KB
  # segment alignment only when asked (the default from r28 on). Checked below.
  -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON
  -DBRULION_WERROR="${BRULION_WERROR:-OFF}"
  -DBRULION_ANDROID_VERSION_CODE="${BRULION_ANDROID_VERSION_CODE:-}"
)
[[ ${BRULION_ANDROID_SELFTEST:-0} == 1 ]] && cmake_args+=(-DBRULION_ANDROID_SELFTEST=ON)
[[ -n ${BRULION_ANDROID_OPENSSL_DIR:-} ]] && cmake_args+=(-DBRULION_ANDROID_OPENSSL_DIR="$BRULION_ANDROID_OPENSSL_DIR")
for abi in "${ABIS[@]:1}"; do
  cmake_args+=("-DQT_PATH_ANDROID_ABI_$abi=$QT_ROOT/$QT_VERSION/$(qt_arch "$abi")")
done
if command -v ccache >/dev/null; then
  cmake_args+=(-DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache)
fi

info "Configuring ($BUILD_DIR)"
cmake "${cmake_args[@]}"

android_build=$BUILD_DIR/src/app/android-build
selftest_build=$BUILD_DIR/src/platform/android/selftest/android-build
# Never pick up an APK of an earlier run.
rm -rf -- "$android_build/build/outputs" "$selftest_build/build/outputs"

targets=(brulion_make_apk)
[[ ${BRULION_ANDROID_AAB:-0} == 1 ]] && targets+=(brulion_make_aab)
[[ ${BRULION_ANDROID_SELFTEST:-0} == 1 ]] && targets+=(brulion_android_selftest_make_apk)
info "Building ${targets[*]} (-j$JOBS)"
# One target at a time: brulion_make_apk and brulion_make_aab both run androiddeployqt and Gradle
# in the same android-build/ directory, and together they break each other's outputs.
for target in "${targets[@]}"; do
  cmake --build "$BUILD_DIR" --parallel "$JOBS" --target "$target"
done

# --- sign --------------------------------------------------------------------------------------

info "Build outputs"
find "$android_build/build/outputs" -type f \( -name '*.apk' -o -name '*.aab' \) -print | sort
newest_apk() { # <android-build dir>
  find "$1/build/outputs/apk" -type f -name '*.apk' -printf '%T@ %p\n' 2>/dev/null | sort -rn | head -n 1 | cut -d' ' -f2-
}

mkdir -p "$OUT_DIR"
abi_tag=$(IFS=_ && echo "${ABIS[*]}")
aligned=$OUT_DIR/.brulion-aligned.apk
cleanup_files+=("$ROOT/$aligned")
apksigner=(java -jar "$BT/lib/apksigner.jar")

sign_apk() { # <input apk> <output apk>
  info "Signing $1"
  # 16 KB alignment of uncompressed native libraries (Android 15+ devices with 16 KB pages).
  "$BT/zipalign" -P 16 -f 4 "$1" "$aligned"
  "${apksigner[@]}" sign --ks "$QT_ANDROID_KEYSTORE_PATH" --ks-key-alias "$QT_ANDROID_KEYSTORE_ALIAS" \
    --ks-pass env:QT_ANDROID_KEYSTORE_STORE_PASS --key-pass env:QT_ANDROID_KEYSTORE_KEY_PASS \
    --out "$2" "$aligned"
  rm -f -- "$2.idsig"
}

apk_in=$(newest_apk "$android_build")
[[ -n $apk_in ]] || die "androiddeployqt produced no APK under $android_build/build/outputs/apk"
apk=$OUT_DIR/brulion-$VERSION-$abi_tag.apk
sign_apk "$apk_in" "$apk"
selftest_apk=
if [[ ${BRULION_ANDROID_SELFTEST:-0} == 1 ]]; then
  selftest_in=$(newest_apk "$selftest_build")
  [[ -n $selftest_in ]] || die "no self-test APK under $selftest_build/build/outputs/apk"
  selftest_apk=$OUT_DIR/brulion-selftest-$VERSION-$abi_tag.apk
  sign_apk "$selftest_in" "$selftest_apk"
fi

aab=
if [[ ${BRULION_ANDROID_AAB:-0} == 1 ]]; then
  aab_in=$(find "$android_build/build/outputs/bundle" -type f -name '*.aab' | sort | head -n 1)
  [[ -n $aab_in ]] || die "no AAB under $android_build/build/outputs/bundle"
  aab=$OUT_DIR/brulion-$VERSION-$abi_tag.aab
  cp -- "$aab_in" "$aab"
  jarsigner -keystore "$QT_ANDROID_KEYSTORE_PATH" -storepass:env QT_ANDROID_KEYSTORE_STORE_PASS \
    -keypass:env QT_ANDROID_KEYSTORE_KEY_PASS "$aab" "$QT_ANDROID_KEYSTORE_ALIAS" >/dev/null
fi

# --- check the APK -----------------------------------------------------------------------------

info "Checking $apk"
"${apksigner[@]}" verify --print-certs "$apk" | grep -E 'Signer #1 certificate (DN|SHA-256)' ||
  die "apksigner cannot verify $apk"
badging=$("$BT/aapt2" dump badging "$apk")
grep -E "^(package|minSdkVersion|sdkVersion|targetSdkVersion|application-label|native-code):" <<<"$badging"
grep -q "^package: name='io.github.januszbit.brulion'" <<<"$badging" || die "wrong package name"
for permission in MANAGE_EXTERNAL_STORAGE POST_NOTIFICATIONS SCHEDULE_EXACT_ALARM USE_EXACT_ALARM \
  RECEIVE_BOOT_COMPLETED VIBRATE WAKE_LOCK USE_FULL_SCREEN_INTENT INTERNET ACCESS_NETWORK_STATE; do
  grep -q "uses-permission: name='android.permission.$permission'" <<<"$badging" ||
    die "permission $permission missing from the merged manifest"
done
echo "permissions: $(grep -c '^uses-permission:' <<<"$badging") (all required present)"
manifest=$("$BT/aapt2" dump xmltree --file AndroidManifest.xml "$apk")
for component in BrulionActivity AlarmActivity ReminderReceiver ActionReceiver RingTimeoutReceiver BootReceiver; do
  grep -q "io.github.januszbit.brulion.android.$component\"" <<<"$manifest" ||
    die "$component missing from the merged manifest"
done
dex_classes=$(unzip -p "$apk" 'classes*.dex' | grep -a -o 'Lio/github/januszbit/brulion/android/[A-Za-z]*;' | sort -u)
for class in BrulionAndroid BrulionActivity AlarmScheduler ReminderNotifier AlarmActivity BootReceiver; do
  grep -q "/$class;" <<<"$dex_classes" || die "class $class missing from classes.dex (Kotlin not compiled?)"
done
echo "Kotlin classes in dex: $(wc -l <<<"$dex_classes")"
# R8 (release builds, android/settings.gradle) must keep the Java classes C++ calls by name.
qt_classes=$(unzip -p "$apk" 'classes*.dex' | grep -a -o 'Lorg/qtproject/qt/android/Qt[A-Za-z]*;' | sort -u)
for class in QtNative QtActivityBase QtLoader; do
  grep -q "/$class;" <<<"$qt_classes" || die "Qt's Java class $class missing from classes.dex (R8 keep rules: android/proguard-rules.pro)"
done
# ... and the androidx members that Qt's C++ calls (FileProvider.getUriForFile, the font download).
androidx_names=$(unzip -p "$apk" 'classes*.dex' | LC_ALL=C grep -a -o -E 'getUriForFile|fetchFonts|Landroidx/core/provider/FontRequest;' | sort -u)
for name in getUriForFile fetchFonts 'Landroidx/core/provider/FontRequest;'; do
  grep -qxF "$name" <<<"$androidx_names" || die "$name missing from classes.dex (R8 keep rules: android/proguard-rules.pro)"
done
echo "classes.dex: $(unzip -l "$apk" 'classes*.dex' | tail -n 1 | awk '{ print $1 }') bytes"
listing=$(unzip -l "$apk")
for abi in "${ABIS[@]}"; do
  grep -q "lib/$abi/libbrulion_$abi.so" <<<"$listing" || die "lib/$abi/libbrulion_$abi.so missing from the APK"
  # HTTPS: Qt's TLS plugin loads OpenSSL 3 at run time (src/app/CMakeLists.txt).
  for lib in libssl_3.so libcrypto_3.so; do
    grep -q "lib/$abi/$lib" <<<"$listing" || die "lib/$abi/$lib missing from the APK: HTTPS (Nextcloud sync) would not work"
  done
done
echo "native libraries: $(grep -c 'lib/.*\.so$' <<<"$listing") in lib/{$(IFS=, && echo "${ABIS[*]}")}"
openssl=$(unzip -p "$apk" "lib/${ABIS[0]}/libcrypto_3.so" | LC_ALL=C grep -a -o -m 1 'OpenSSL 3\.[0-9.]* [0-9]* [A-Z][a-z]* [0-9]*' || true)
echo "OpenSSL: ${openssl:-version not found} (libssl_3.so, libcrypto_3.so)"
if [[ $BUILD_TYPE == Release || $BUILD_TYPE == MinSizeRel ]] && grep -q 'libplugins_qmltooling_' <<<"$listing"; then
  die "QML debugging plugins (qmltooling) in a $BUILD_TYPE APK (src/app/CMakeLists.txt leaves them out)"
fi

# 16 KB pages (Android 15+ devices with 16 KB memory pages refuse other libraries): the 64-bit
# libraries must be stored uncompressed at 16 KB boundaries in the APK and have ELF LOAD
# segments aligned to at least 16 KB. Brulion's own library and the OpenSSL it bundles must
# comply; Qt's prebuilt ones are reported.
"$BT/zipalign" -c -P 16 4 "$apk" >/dev/null || die "$apk is not zip-aligned for 16 KB pages (zipalign -c -P 16 4)"
readelf_bin=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf
if [[ -x $readelf_bin ]]; then
  elf_tmp=$(mktemp -d)
  cleanup_files+=("$elf_tmp")
  unzip -q -o "$apk" 'lib/arm64-v8a/*.so' 'lib/x86_64/*.so' -d "$elf_tmp" 2>/dev/null || true
  misaligned=()
  checked=0
  while IFS= read -r -d '' so; do
    min=0
    while read -r align; do
      value=$((align))
      if ((min == 0 || value < min)); then min=$value; fi
    done < <("$readelf_bin" -lW "$so" | awk '$1 == "LOAD" { print $NF }')
    lib=${so#"$elf_tmp"/}
    ((min > 0)) || die "$lib: llvm-readelf found no LOAD segments"
    checked=$((checked + 1))
    if ((min < 16384)); then
      [[ $lib != */libbrulion_* && $lib != */libssl_3.so && $lib != */libcrypto_3.so ]] ||
        die "$lib: LOAD segments aligned to $min bytes, 16 KB pages need 16384"
      misaligned+=("$lib ($min)")
    fi
  done < <(find "$elf_tmp" -name '*.so' -print0)
  rm -rf -- "$elf_tmp"
  if ((${#misaligned[@]})); then
    warn "${#misaligned[@]} of $checked 64-bit libraries are not 16 KB aligned" \
      "They will not load on Android 15+ devices with 16 KB pages: ${misaligned[*]}"
  else
    echo "16 KB page alignment: $checked 64-bit libraries OK"
  fi
fi

# --- Kotlin unit tests (optional) -------------------------------------------------------------

if [[ ${BRULION_ANDROID_UNIT_TESTS:-0} == 1 ]]; then
  info "Kotlin unit tests (android/tests)"
  (
    cd -- "$android_build"
    BRULION_ANDROID_TESTS_DIR="$ROOT/android/tests/kotlin" ./gradlew --no-daemon --console=plain \
      --init-script "$ROOT/android/tests/unit-tests.gradle" testDebugUnitTest
  )
fi

if [[ ${BRULION_ANDROID_LINT:-0} == 1 ]]; then
  info "Android lint"
  (cd -- "$android_build" && ./gradlew --no-daemon --console=plain lintRelease)
  report=$android_build/build/reports/lint-results-release.txt
  [[ -f $report ]] || die "lint wrote no report ($report)"
  # Issues in Brulion's files (android/ copied into android-build; res/values/libs.xml is Qt's).
  abs_build=$(cd -- "$android_build" && pwd)
  ours=$(sed "s#^$abs_build/##" "$report" | grep -E '^(src/io/github/januszbit/|AndroidManifest\.xml|res/).*(Error|Warning):' |
    grep -v '^res/values/libs\.xml' || true)
  echo "lint: $(tail -n 1 "$report") — in Brulion's files: $(grep -c . <<<"$ours" || true)"
  [[ -z $ours ]] || printf '%s\n' "$ours"
  echo "full report: $report"
fi

# --- results -----------------------------------------------------------------------------------

if [[ -n ${GITHUB_OUTPUT:-} ]]; then
  {
    echo "version=$VERSION"
    echo "apk=$apk"
    echo "aab=$aab"
    echo "signing=$signing"
    echo "openssl=${openssl:-unknown}"
  } >>"$GITHUB_OUTPUT"
fi

info "Done"
(cd "$OUT_DIR" && sha256sum -- "$(basename -- "$apk")" ${aab:+"$(basename -- "$aab")"})
echo "APK: $ROOT/$apk ($(du -h "$apk" | cut -f1), signing: $signing)"
[[ -z $aab ]] || echo "AAB: $ROOT/$aab"
[[ -z $selftest_apk ]] || echo "Self-test APK: $ROOT/$selftest_apk"
