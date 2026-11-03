#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Builds Qt 6.11.2 for Android from Qt's source archives (for builds without prebuilt Qt, e.g.
# F-Droid): first the host Qt with the tools a cross build runs (moc, rcc, qmlcachegen,
# qmltyperegistrar, qmlimportscanner, androiddeployqt, qsb, lrelease…) and Qt's translations,
# then Qt for Android with the NDK (r27c) — only the modules Brulion's Android build uses:
#   host:    qtbase qtshadertools qtsvg qtdeclarative qttools (Linguist tools only) qttranslations
#   Android: qtbase qtshadertools qtsvg qtdeclarative
# Both in the layout aqt installs (scripts/android-build.sh reads it), under QT_ROOT:
#   $QT_ROOT/6.11.2/gcc_64              host Qt (QT_HOST_PATH)
#   $QT_ROOT/6.11.2/android_<arch>      Qt for Android, one per ABI
# Idempotent: a module whose stamp (version, sources, options) matches is not built again.
# Takes ~25 min (16 cores; ~10 GB of disk while building, ~1.5 GB installed).
#
# Usage: scripts/android-qt-from-source.sh [--help]
#
# Environment:
#   BRULION_ANDROID_OPENSSL_DIR  REQUIRED: OpenSSL 3 headers for Qt's TLS plugin (it loads
#                                libssl_3.so at run time): the output of
#                                scripts/android-openssl-from-source.sh (ssl_3/<abi>/include)
#   QT_ROOT                      install root (default ~/.cache/brulion-qt-source)
#   BRULION_QT_SOURCE_DIR        a Qt source tree with qtbase/, qtdeclarative/… at v6.11.2 (e.g.
#                                F-Droid's srclib Qt5@v6.11.2 after `init-repository`) instead of
#                                the pinned source archives
#   BRULION_QT_DOWNLOADS         where the archives are downloaded (default ~/.cache/brulion-src-downloads)
#   BRULION_ANDROID_ABIS         ABIs, space separated (default "arm64-v8a"; also armeabi-v7a, x86_64, x86)
#   ANDROID_SDK_ROOT, ANDROID_NDK_ROOT, NDK_VERSION   like scripts/android-build.sh
#   JAVA_HOME                    JDK 17+ (Qt's Java classes); default: the `java` on PATH
#   BRULION_ANDROID_CMAKE=system the cmake/ninja on PATH instead of the SDK's cmake;3.31.6
#   BRULION_QT_KEEP_BUILD=1      keep the build directories ($QT_ROOT/build) after installing
#   JOBS                         parallel compile jobs (default: all cores)
# Needs a host C++ compiler (g++ or clang++), and the Android SDK packages cmake;3.31.6 and
# platforms;android-36 (install them with sdkmanager, or let scripts/android-build.sh do it).
# On NixOS the script re-runs itself in `nix shell` (gcc, JDK 21, ccache).
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

QT_VERSION=6.11.2
# SHA-256 of https://download.qt.io/official_releases/qt/6.11/6.11.2/submodules/<module>-everywhere-src-6.11.2.tar.xz
# (the same values Qt publishes in <archive>.sha256). Update together with QT_VERSION.
declare -A QT_SHA256=(
  [qtbase]=5b2e00eccaf5a4d8c14134ffa0ea8dfd0a35ae1ffc7f8d87fa4305a1ed23cf22
  [qtshadertools]=805046b8b7757665586890b375940047e874ae3ab00adb6d3f2b38fc6b200b1c
  [qtsvg]=d594337feca84c26fb67fe87b85e6a5c12fda404b611d905f9d138210c311876
  [qtdeclarative]=215b7b70517e380123eabc6b92243f3c47b6f016a91d126057dbe53551c6b430
  [qttools]=9ea75af35c512f7e09e61c8c3af3997f13b4d43bb099cf43fcec470126b4041e
  [qttranslations]=021684c1a7937a9fabc3b056a6698ad5978794caf9ac190fd6cc11399e67c014
)
# In build order (each module needs the ones before it).
HOST_MODULES=(qtbase qtshadertools qtsvg qtdeclarative qttools qttranslations)
ANDROID_MODULES=(qtbase qtshadertools qtsvg qtdeclarative)
SDK_CMAKE=3.31.6
QT_MIRRORS=(
  https://download.qt.io/official_releases/qt
  https://ftp.fau.de/qtproject/official_releases/qt
  https://mirrors.ocf.berkeley.edu/qt/official_releases/qt
)

[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || die "needs an x86_64 Linux host (the NDK is x86_64 only)"

if [[ -e /etc/NIXOS && -z ${BRULION_QT_IN_NIX_SHELL:-} ]]; then
  info "NixOS: entering nix shell (gcc, JDK 21, ccache)"
  export BRULION_QT_IN_NIX_SHELL=1
  unset JAVA_HOME
  exec nix shell nixpkgs#gcc nixpkgs#jdk21_headless nixpkgs#ccache nixpkgs#gnumake --command bash "$SCRIPT" "$@"
fi

# --- configuration ------------------------------------------------------------------------------

QT_ROOT=${QT_ROOT:-$HOME/.cache/brulion-qt-source}
mkdir -p "$QT_ROOT"
QT_ROOT=$(cd -- "$QT_ROOT" && pwd)
HOST_PREFIX=$QT_ROOT/$QT_VERSION/gcc_64
DOWNLOADS=${BRULION_QT_DOWNLOADS:-$HOME/.cache/brulion-src-downloads}
BUILD_ROOT=$QT_ROOT/build
JOBS=${JOBS:-$(nproc)}
read -r -a ABIS <<<"${BRULION_ANDROID_ABIS:-arm64-v8a}"
((${#ABIS[@]})) || die "BRULION_ANDROID_ABIS is empty"
NDK_VERSION=${NDK_VERSION:-27.2.12479018}
SDK=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/.cache/brulion-android-sdk}}
[[ -d $SDK ]] || die "Android SDK not found at $SDK (set ANDROID_SDK_ROOT)"
SDK=$(cd -- "$SDK" && pwd)
NDK=${ANDROID_NDK_ROOT:-$SDK/ndk/$NDK_VERSION}
[[ -f $NDK/source.properties ]] || die "Android NDK not found at $NDK (set ANDROID_NDK_ROOT)"
NDK=$(cd -- "$NDK" && pwd)
ndk_revision=$(sed -nE 's/^Pkg\.Revision *= *([0-9.]+).*/\1/p' "$NDK/source.properties")
[[ -n ${BRULION_ANDROID_OPENSSL_DIR:-} ]] ||
  die "BRULION_ANDROID_OPENSSL_DIR is not set: build OpenSSL first (scripts/android-openssl-from-source.sh) — Qt's TLS plugin needs its headers"
OPENSSL_DIR=$(cd -- "$BRULION_ANDROID_OPENSSL_DIR" && pwd) || die "BRULION_ANDROID_OPENSSL_DIR=$BRULION_ANDROID_OPENSSL_DIR does not exist"

qt_arch() { # ABI -> aqt arch / Qt directory name
  case $1 in
    arm64-v8a) echo android_arm64_v8a ;;
    armeabi-v7a) echo android_armv7 ;;
    x86_64) echo android_x86_64 ;;
    x86) echo android_x86 ;;
    *) die "unknown Android ABI: $1" ;;
  esac
}

if [[ ${BRULION_ANDROID_CMAKE:-sdk} != system ]]; then
  # The SDK's CMake (NixOS's patched cmake does not search the NDK sysroot's /usr: no EGL/GLES).
  [[ -x $SDK/cmake/$SDK_CMAKE/bin/cmake ]] ||
    die "$SDK has no cmake;$SDK_CMAKE (sdkmanager --install 'cmake;$SDK_CMAKE', or BRULION_ANDROID_CMAKE=system)"
  export PATH="$SDK/cmake/$SDK_CMAKE/bin:$PATH"
fi
{ command -v cmake && command -v ninja; } >/dev/null || die "cmake and ninja are needed"
command -v c++ >/dev/null || die "a host C++ compiler (c++) is needed for the host Qt"

if [[ -z ${JAVA_HOME:-} ]]; then
  java=$(command -v java || true)
  [[ -n $java ]] || die "no JDK: install JDK 21 (or 17+) or set JAVA_HOME"
  JAVA_HOME=$(dirname -- "$(dirname -- "$(readlink -f -- "$java")")")
fi
export JAVA_HOME PATH="$JAVA_HOME/bin:$PATH"
[[ -x $JAVA_HOME/bin/javac ]] || die "JAVA_HOME=$JAVA_HOME has no bin/javac (a JDK, not a JRE, is needed)"
export ANDROID_SDK_ROOT=$SDK ANDROID_HOME=$SDK ANDROID_NDK_ROOT=$NDK
# No locale-, time zone- or user-dependent tool output in the build.
export LC_ALL=C TZ=UTC

# Options of qtbase's configure (the other modules take over its build type and settings;
# qt-configure-module accepts only features and CMake variables).
qtbase_opts=(-release -opensource -confirm-license -nomake tests -nomake examples)
command -v ccache >/dev/null && qtbase_opts+=(-ccache)
# For the other modules: CMake variables (not in the stamps: always the same).
module_cmake=(-DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF)

info "Qt $QT_VERSION from source: host + Android ${ABIS[*]} (NDK $ndk_revision) -> $QT_ROOT/$QT_VERSION"
echo "cmake: $(command -v cmake) ($(cmake --version | head -n 1 | cut -d' ' -f3)), c++: $(c++ --version | head -n 1)"
echo "JDK: $JAVA_HOME"

# --- sources ------------------------------------------------------------------------------------

if [[ -n ${BRULION_QT_SOURCE_DIR:-} ]]; then
  SRC=$(cd -- "$BRULION_QT_SOURCE_DIR" && pwd) || die "BRULION_QT_SOURCE_DIR=$BRULION_QT_SOURCE_DIR does not exist"
  for module in "${HOST_MODULES[@]}"; do
    conf=$SRC/$module/.cmake.conf
    [[ -f $conf ]] || die "$SRC/$module is missing (init-repository --module-subset=$(IFS=, && echo "${HOST_MODULES[*]}"))"
    grep -q "QT_REPO_MODULE_VERSION \"$QT_VERSION\"" "$conf" || die "$SRC/$module is not Qt $QT_VERSION ($conf)"
  done
  src_id="source tree $SRC"
else
  SRC=$QT_ROOT/src/$QT_VERSION
  for module in "${HOST_MODULES[@]}"; do
    name=$module-everywhere-src-$QT_VERSION.tar.xz
    archive=$DOWNLOADS/$name
    if [[ ! -f $archive ]] || ! echo "${QT_SHA256[$module]}  $archive" | sha256sum -c --quiet - 2>/dev/null; then
      mkdir -p "$DOWNLOADS"
      ok=0
      for mirror in "${QT_MIRRORS[@]}"; do
        url=$mirror/${QT_VERSION%.*}/$QT_VERSION/submodules/$name
        info "Downloading $url"
        if curl -fL --retry 3 -o "$archive.part" "$url"; then
          ok=1
          break
        fi
      done
      ((ok)) || die "cannot download $name"
      mv -- "$archive.part" "$archive"
    fi
    echo "${QT_SHA256[$module]}  $archive" | sha256sum -c --quiet - ||
      die "$archive: SHA-256 $(sha256sum "$archive" | cut -d' ' -f1), expected ${QT_SHA256[$module]}"
    if [[ ! -f $SRC/$module/.brulion-sha256 || $(<"$SRC/$module/.brulion-sha256") != "${QT_SHA256[$module]}" ]]; then
      echo "Unpacking $name"
      rm -rf -- "${SRC:?}/$module" "${SRC:?}/$module-everywhere-src-$QT_VERSION"
      mkdir -p "$SRC"
      tar -C "$SRC" -xJf "$archive"
      mv -- "$SRC/$module-everywhere-src-$QT_VERSION" "$SRC/$module"
      printf '%s\n' "${QT_SHA256[$module]}" >"$SRC/$module/.brulion-sha256"
    fi
  done
  src_id="archives $(for m in "${HOST_MODULES[@]}"; do printf '%s ' "${QT_SHA256[$m]:0:12}"; done)"
fi

# --- build helpers ------------------------------------------------------------------------------

# Build paths do not end up in the libraries (debug info, __FILE__): the same sources give the
# same Qt in any directory (F-Droid's build server, GitHub Actions, a laptop).
prefix_maps() { # <build dir> <prefix>
  printf '%s' "-ffile-prefix-map=$SRC=/qt-src -ffile-prefix-map=$1=/qt-build -ffile-prefix-map=$2=/qt -ffile-prefix-map=$NDK=/ndk"
}

# build_module <name> <build dir> <prefix> <stamp text> <configure command…>
# The stamp also covers the modules built before it into the same prefix (chain): a rebuilt
# qtbase rebuilds everything on top of it.
chain=
build_module() {
  local module=$1 build=$2 prefix=$3 stamp_text
  stamp_text="$4 after $(printf '%s' "$chain" | sha256sum | cut -c1-16)"
  shift 4
  local stamp=$prefix/.brulion-$module.stamp
  chain+=$stamp_text
  if [[ -f $stamp && $(<"$stamp") == "$stamp_text" ]]; then
    echo "$module: up to date ($prefix)"
    return 0
  fi
  info "Building $module -> $prefix"
  rm -rf -- "$build"
  mkdir -p "$build"
  local log=$build/build.log
  (
    cd -- "$build"
    "$@" >"$log" 2>&1 || {
      tail -n 60 "$log" >&2
      echo "configure of $module failed (log: $log)" >&2
      exit 1
    }
    # Ninja prints only failing commands (and the last line of progress) to the log.
    cmake --build . --parallel "$JOBS" >>"$log" 2>&1 || {
      grep -E -B2 -A12 'FAILED:|error:' "$log" | head -n 80 >&2
      echo "build of $module failed (log: $log)" >&2
      exit 1
    }
    cmake --install . >>"$log" 2>&1 || {
      tail -n 30 "$log" >&2
      exit 1
    }
  ) || die "$module ($prefix) failed"
  printf '%s\n' "$stamp_text" >"$stamp"
  [[ ${BRULION_QT_KEEP_BUILD:-0} == 1 ]] || rm -rf -- "$build"
  echo "$module: installed"
}

# Features nobody uses on either side: SQL, printing, D-Bus, widgets (Brulion's Android UI is Qt
# Quick; uic is therefore not needed), system libraries (the bundled zlib, PCRE2, PNG, JPEG,
# FreeType, HarfBuzz behave the same on every build machine: rcc's zlib output does not depend on
# the host's zlib), zstd (rcc would compress with it, Qt for Android cannot read it).
common_features=(
  -DFEATURE_sql=OFF -DFEATURE_printsupport=OFF -DFEATURE_dbus=OFF -DFEATURE_widgets=OFF
  -DFEATURE_zstd=OFF -DFEATURE_brotli=OFF
  -DFEATURE_system_zlib=OFF -DFEATURE_system_pcre2=OFF -DFEATURE_system_png=OFF
  -DFEATURE_system_jpeg=OFF -DFEATURE_system_freetype=OFF -DFEATURE_system_harfbuzz=OFF
  -DFEATURE_system_doubleconversion=OFF -DFEATURE_system_textmarkdownreader=OFF
  -DFEATURE_icu=OFF -DFEATURE_glib=OFF -DFEATURE_gssapi=OFF -DFEATURE_libproxy=OFF
  -DFEATURE_qmlls=OFF
  -DCMAKE_INSTALL_MESSAGE=NEVER -Wno-dev
)
# qtdeclarative, both sides: Qt Quick Controls only with the Basic style (the app's QML imports
# QtQuick.Controls.Basic; android-build.sh refuses APKs with other styles), no QtQuick.Dialogs
# (FilePicker instead), no Qt Design Studio helpers — half of qtdeclarative's build time.
declarative_features=(
  -DFEATURE_quickcontrols2_fluentwinui3=OFF -DFEATURE_quickcontrols2_fusion=OFF
  -DFEATURE_quickcontrols2_imagine=OFF -DFEATURE_quickcontrols2_ios=OFF
  -DFEATURE_quickcontrols2_macos=OFF -DFEATURE_quickcontrols2_material=OFF
  -DFEATURE_quickcontrols2_universal=OFF -DFEATURE_quickcontrols2_windows=OFF
  -DFEATURE_quickcontrols2_stylekit=OFF -DFEATURE_quick_dialogs=OFF -DFEATURE_quick_designer=OFF
  -DFEATURE_quickshapes_designhelpers=OFF
)
# The host Qt only runs tools: no OpenGL (-no-opengl), X11 or Wayland (the offscreen platform is enough for
# qsb and svgtoqml), no TLS; qttools only with the Linguist tools (lrelease, lupdate).
host_features=(
  -DFEATURE_vulkan=OFF -DFEATURE_xcb=OFF -DFEATURE_xlib=OFF
  -DFEATURE_wayland=OFF -DFEATURE_xkbcommon=OFF -DFEATURE_fontconfig=OFF -DFEATURE_libudev=OFF
  -DFEATURE_evdev=OFF -DFEATURE_libinput=OFF -DFEATURE_mtdev=OFF -DFEATURE_tslib=OFF
  -DFEATURE_linuxfb=OFF -DFEATURE_eglfs=OFF -DFEATURE_vnc=OFF -DFEATURE_openssl=OFF
  -DFEATURE_assistant=OFF -DFEATURE_designer=OFF -DFEATURE_distancefieldgenerator=OFF
  -DFEATURE_pixeltool=OFF -DFEATURE_qtdiag=OFF -DFEATURE_qtplugininfo=OFF -DFEATURE_qdbus=OFF
  -DFEATURE_qdoc=OFF -DFEATURE_clang=OFF -DFEATURE_qev=OFF -DFEATURE_linguist=ON
)

# --- host Qt ------------------------------------------------------------------------------------

cxx_id=$(c++ --version | head -n 1)
for module in "${HOST_MODULES[@]}"; do
  build=$BUILD_ROOT/host-$module
  stamp="qt $QT_VERSION host $module ($src_id) features ${common_features[*]} ${host_features[*]} ($cxx_id)"
  [[ $module == qtdeclarative ]] && stamp+=" ${declarative_features[*]}"
  if [[ $module == qtbase ]]; then
    configure=("$SRC/qtbase/configure" -prefix "$HOST_PREFIX" -no-opengl "${qtbase_opts[@]}")
    extra=()
  else
    configure=("$HOST_PREFIX/bin/qt-configure-module" "$SRC/$module")
    extra=("${module_cmake[@]}")
  fi
  [[ $module == qtdeclarative ]] && extra+=("${declarative_features[@]}")
  # Host tools: optimised, no debug info, nothing of the host's paths matters for the APK.
  CFLAGS='' CXXFLAGS='' build_module "$module" "$build" "$HOST_PREFIX" "$stamp" \
    "${configure[@]}" -- "${common_features[@]}" "${host_features[@]}" "${extra[@]}"
done
for tool in libexec/moc libexec/rcc libexec/qmlcachegen libexec/qmltyperegistrar libexec/qmlimportscanner \
  libexec/qsb bin/qsb bin/androiddeployqt bin/lrelease; do
  [[ $tool == libexec/qsb && ! -e $HOST_PREFIX/$tool ]] && continue
  [[ $tool == bin/qsb && ! -e $HOST_PREFIX/$tool ]] && continue
  [[ -x $HOST_PREFIX/$tool ]] || die "host tool $HOST_PREFIX/$tool was not built"
done
[[ -x $HOST_PREFIX/libexec/qsb || -x $HOST_PREFIX/bin/qsb ]] || die "host tool qsb was not built"
[[ -f $HOST_PREFIX/translations/qtbase_pl.qm ]] || die "Qt's translations (qtbase_pl.qm) were not built"

# --- Qt for Android -----------------------------------------------------------------------------

host_chain=$chain
for abi in "${ABIS[@]}"; do
  chain=$host_chain
  prefix=$QT_ROOT/$QT_VERSION/$(qt_arch "$abi")
  ssl=$OPENSSL_DIR/ssl_3/$abi
  [[ -f $ssl/include/openssl/ssl.h ]] ||
    die "no OpenSSL headers for $abi in $ssl/include (scripts/android-openssl-from-source.sh with BRULION_ANDROID_ABIS=\"${ABIS[*]}\")"
  ssl_id=$(cat "$ssl/include/openssl/opensslv.h" "$ssl/include/openssl/configuration.h" | sha256sum | cut -c1-16)
  android_features=(
    -DOPENSSL_ROOT_DIR="$ssl" -DOPENSSL_INCLUDE_DIR="$ssl/include"
    # 16 KB pages (Android 15+ devices): NDK r27 aligns 64-bit libraries to 16 KB only when asked.
    -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON
  )
  for module in "${ANDROID_MODULES[@]}"; do
    build=$BUILD_ROOT/$abi-$module
    stamp="qt $QT_VERSION $abi $module ($src_id) ndk $ndk_revision openssl-headers $ssl_id features ${common_features[*]} ${android_features[*]}"
    [[ $module == qtdeclarative ]] && stamp+=" ${declarative_features[*]}"
    if [[ $module == qtbase ]]; then
      configure=("$SRC/qtbase/configure" -prefix "$prefix" -platform android-clang
        -android-sdk "$SDK" -android-ndk "$NDK" -android-abis "$abi" -qt-host-path "$HOST_PREFIX"
        -openssl-runtime "${qtbase_opts[@]}")
      extra=()
    else
      configure=("$prefix/bin/qt-configure-module" "$SRC/$module")
      extra=("${module_cmake[@]}")
    fi
    [[ $module == qtdeclarative ]] && extra+=("${declarative_features[@]}")
    flags=$(prefix_maps "$build" "$prefix")
    CFLAGS=$flags CXXFLAGS=$flags build_module "$module" "$build" "$prefix" "$stamp" \
      "${configure[@]}" -- "${common_features[@]}" "${android_features[@]}" "${extra[@]}"
  done
  [[ -f $prefix/lib/cmake/Qt6/qt.toolchain.cmake ]] || die "$prefix has no lib/cmake/Qt6/qt.toolchain.cmake"
  [[ -f $prefix/plugins/tls/libplugins_tls_qopensslbackend_$abi.so ]] ||
    die "Qt for $abi has no OpenSSL TLS plugin (plugins/tls/libplugins_tls_qopensslbackend_$abi.so): HTTPS would not work"
  echo "Qt for Android $abi: $prefix"
done

if [[ ${BRULION_QT_KEEP_BUILD:-0} != 1 ]]; then
  rmdir -- "$BUILD_ROOT" 2>/dev/null || true
fi
info "Done: QT_ROOT=$QT_ROOT (host: $HOST_PREFIX)"
