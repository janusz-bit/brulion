#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Builds Brulion's Linux AppImage for the architecture of this machine (x86_64 or aarch64):
# Qt 6.11.2 from aqt, a CMake Release build + unit tests, `cmake --install` into an AppDir,
# then linuxdeploy + linuxdeploy-plugin-qt bundle Qt (libraries; xcb, Wayland and offscreen
# platform plugins; the QML modules that src/app/qml imports) and pack the AppImage.
# Idempotent: re-running reuses the build directory, Qt and the downloaded tools.
#
# An AppImage runs on distributions whose glibc is at least as new as the build machine's, so
# build on an old distribution: x86_64 in an ubuntu:22.04 container (glibc 2.35; Qt 6.11's
# x86_64 binaries need 2.34), aarch64 on Ubuntu 24.04 (Qt's arm64 binaries need glibc 2.38).
# CI: .github/workflows/appimage.yml. Locally with podman (or docker), from the repository root:
#   podman run --rm -v "$PWD:/src:Z" -w /src docker.io/library/ubuntu:22.04 \
#     bash -c 'scripts/appimage-build.sh --install-deps && scripts/appimage-build.sh'
# Not on NixOS itself (the binaries would point into /nix/store): NixOS uses `nix build .#brulion`.
#
# Usage: scripts/appimage-build.sh [--install-deps | --help]
#   --install-deps   install the build dependencies with apt-get (Debian/Ubuntu; sudo when not
#                    root) and exit
#
# Environment (all optional):
#   QT_VERSION              Qt version (default 6.11.2)
#   QT_ROOT                 aqt install root (default ~/.cache/brulion-qt), shared with
#                           scripts/android-build.sh and scripts/wasm-build.sh
#   QT_DIR                  desktop Qt (default $QT_ROOT/$QT_VERSION/gcc_64, on aarch64 .../gcc_arm64);
#                           installed with aqt when missing
#   BUILD_DIR               build directory (default build-appimage)
#   DIST_DIR                output directory (default dist-appimage)
#   JOBS                    parallel compile jobs (default: all cores)
#   BRULION_WERROR          ON (default) = warnings in Brulion's own code are errors
#   BRULION_APPIMAGE_TESTS  1 (default) = run the unit tests (ctest) before packaging
#   LINUXDEPLOY_DIR         where linuxdeploy is downloaded (default ~/.cache/brulion-linuxdeploy)
#   LDAI_UPDATE_INFORMATION AppImage update information (e.g. gh-releases-zsync|owner|repo|latest|
#                           Brulion-*-x86_64.AppImage.zsync); also writes the .zsync file (needs
#                           zsyncmake, from the zsync package)
#
# Output: dist-appimage/Brulion-<version>-<arch>.AppImage, with update information also
# dist-appimage/Brulion-<version>-<arch>.AppImage.zsync (publish both in the same release).
# On GitHub Actions also written to $GITHUB_OUTPUT as `appimage` and `version`.
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
SCRIPT="$ROOT/scripts/$(basename -- "${BASH_SOURCE[0]}")"
cd "$ROOT"

info() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
die() {
  if [[ -n ${GITHUB_ACTIONS:-} ]]; then echo "::error::$*"; fi
  printf '\033[1;31merror:\033[0m %s\n' "$*" >&2
  exit 1
}

QT_VERSION=${QT_VERSION:-6.11.2}
AQT_VERSION=3.3.0
CMAKE_MIN=3.24
# linuxdeploy releases (tags) and the SHA-256 of their AppImages. Bump together.
LINUXDEPLOY_TAG=1-alpha-20251107-1
LINUXDEPLOY_QT_TAG=1-alpha-20250213-1
declare -A SHA256=(
  [linuxdeploy-x86_64.AppImage]=c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
  [linuxdeploy-aarch64.AppImage]=620095110d693282b8ebeb244a95b5e911cf8f65f76c88b4b47d16ae6346fcff
  [linuxdeploy-plugin-qt-x86_64.AppImage]=15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724
  [linuxdeploy-plugin-qt-aarch64.AppImage]=bf1c24aff6d749b5cf423afad6f15abd4440f81dec1aab95706b25f6667cdcf1
)

ARCH=$(uname -m)
case $ARCH in
  x86_64) qt_subdir=gcc_64 aqt_host=linux aqt_arch=linux_gcc_64 ;;
  aarch64) qt_subdir=gcc_arm64 aqt_host=linux_arm64 aqt_arch=linux_gcc_arm64 ;;
  *) die "unsupported architecture $ARCH (x86_64 or aarch64)" ;;
esac

install_deps() {
  command -v apt-get >/dev/null || die "--install-deps supports Debian/Ubuntu (apt-get) only"
  local sudo=()
  ((EUID == 0)) || sudo=(sudo)
  export DEBIAN_FRONTEND=noninteractive
  "${sudo[@]}" apt-get update -q
  local packages=(
    # build tools (python3-venv: aqtinstall, and CMake >= 3.24 on Ubuntu 22.04)
    build-essential git curl ca-certificates file zstd zsync xz-utils python3 python3-venv ninja-build
    # time zone database: the tests pin TZ=Europe/Warsaw (minimal containers ship without it)
    tzdata
    # needed to compile against Qt Gui
    libgl-dev libegl-dev libopengl-dev libxkbcommon-dev libvulkan-dev
    # libraries of Qt and of the xcb/Wayland platform plugins, bundled by linuxdeploy
    libfontconfig1 libfreetype6 libdbus-1-3 libglib2.0-0 libgssapi-krb5-2 libbrotli1 libzstd1
    libx11-6 libx11-xcb1 libxcb1 libxcb-cursor0 libxcb-glx0 libxcb-icccm4 libxcb-image0
    libxcb-keysyms1 libxcb-randr0 libxcb-render0 libxcb-render-util0 libxcb-shape0 libxcb-shm0
    libxcb-sync1 libxcb-util1 libxcb-xfixes0 libxcb-xkb1 libxkbcommon0 libxkbcommon-x11-0
    libsm6 libice6 libwayland-client0 libwayland-cursor0 libwayland-egl1 libegl1 libgl1
  )
  # Ubuntu 22.04's default g++ 11 lacks parts of C++20; g++-12 is in its main archive.
  local default_gcc
  default_gcc=$(apt-cache show --no-all-versions gcc 2>/dev/null | sed -nE 's/^Version: ([0-9]+:)?([0-9]+).*/\2/p' | head -n 1)
  if ((${default_gcc:-0} < 12)) && apt-cache show g++-12 >/dev/null 2>&1; then
    packages+=(g++-12)
  fi
  "${sudo[@]}" apt-get install -y -q --no-install-recommends "${packages[@]}"
}

case ${1:-} in
  -h | --help)
    awk 'NR > 2 && /^#/ { sub(/^# ?/, ""); print; next } NR > 2 { exit }' "$SCRIPT"
    exit 0
    ;;
  --install-deps)
    install_deps
    exit 0
    ;;
  "") ;;
  *) die "unknown argument: $1 (see --help)" ;;
esac

[[ ! -e /etc/NIXOS ]] || die "NixOS: build the AppImage in a container (see --help), or use nix build .#brulion"

QT_ROOT=${QT_ROOT:-$HOME/.cache/brulion-qt}
QT_DIR=${QT_DIR:-$QT_ROOT/$QT_VERSION/$qt_subdir}
BUILD_DIR=${BUILD_DIR:-build-appimage}
DIST_DIR=${DIST_DIR:-dist-appimage}
JOBS=${JOBS:-$(nproc)}
LINUXDEPLOY_DIR=${LINUXDEPLOY_DIR:-$HOME/.cache/brulion-linuxdeploy}
TOOLS_VENV=${XDG_CACHE_HOME:-$HOME/.cache}/brulion-appimage-venv
VERSION=$(sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt | head -n 1)
[[ -n $VERSION ]] || die "cannot read the project version from CMakeLists.txt"
APPDIR=$BUILD_DIR/AppDir
APP_ID=io.github.januszbit.brulion

info "Brulion $VERSION AppImage ($ARCH), Qt $QT_VERSION"

# --- Python tools (aqtinstall; CMake when the system one is too old) ---------------------------

tools_venv() {
  if [[ ! -x $TOOLS_VENV/bin/pip ]]; then
    python3 -m venv "$TOOLS_VENV" || die "python3 -m venv failed (install python3-venv, or run --install-deps)"
    "$TOOLS_VENV/bin/pip" install --quiet --upgrade pip
  fi
}

aqt() {
  if command -v uvx >/dev/null; then
    uvx --from "aqtinstall==$AQT_VERSION" aqt "$@"
  elif command -v pipx >/dev/null; then
    pipx run --spec "aqtinstall==$AQT_VERSION" aqt "$@"
  else
    tools_venv
    [[ -x $TOOLS_VENV/bin/aqt ]] || "$TOOLS_VENV/bin/pip" install --quiet "aqtinstall==$AQT_VERSION"
    # aqtinstall tries each archive as ZIP before 7z, and before Python 3.14 zipfile.is_zipfile()
    # takes some .7z files for ZIP (Qt 6.11.2's linux_arm64 qttools: "Bad offset for central
    # directory", on every mirror). A .pth line, run by every interpreter of the venv (also aqt's
    # spawned workers), keeps .7z files out of ZIP detection.
    local site
    site=$("$TOOLS_VENV/bin/python" -c 'import sysconfig; print(sysconfig.get_paths()["purelib"])')
    printf '%s\n' "import zipfile; _is_zip = zipfile.is_zipfile; zipfile.is_zipfile = lambda f: not str(getattr(f, 'name', f)).lower().endswith('.7z') and _is_zip(f)" \
      >"$site/brulion-7z-not-zip.pth"
    "$TOOLS_VENV/bin/aqt" "$@"
  fi
}

cmake_ok() {
  command -v cmake >/dev/null && command -v ninja >/dev/null || return 1
  local have
  have=$(cmake --version | sed -nE '1s/^cmake version ([0-9]+\.[0-9]+).*/\1/p')
  printf '%s\n%s\n' "$CMAKE_MIN" "$have" | sort -C -V
}
if [[ -x $TOOLS_VENV/bin/cmake ]]; then export PATH="$TOOLS_VENV/bin:$PATH"; fi
if ! cmake_ok; then
  info "Installing CMake >= $CMAKE_MIN and Ninja into $TOOLS_VENV"
  tools_venv
  "$TOOLS_VENV/bin/pip" install --quiet "cmake>=$CMAKE_MIN,<5" ninja
  export PATH="$TOOLS_VENV/bin:$PATH"
  cmake_ok || die "no usable cmake >= $CMAKE_MIN and ninja"
fi

# Prefer g++ 12+ (C++20); Ubuntu 22.04 defaults to 11.
if [[ -z ${CXX:-} ]]; then
  for v in 14 13 12; do
    if command -v "g++-$v" >/dev/null; then
      export CC=gcc-$v CXX=g++-$v
      break
    fi
  done
fi
echo "CMake $(cmake --version | head -n 1 | cut -d' ' -f3), Ninja $(ninja --version), ${CXX:-c++} $("${CXX:-c++}" -dumpversion)"

# --- Qt -----------------------------------------------------------------------------------------

if [[ ! -f $QT_DIR/lib/cmake/Qt6/Qt6Config.cmake ]]; then
  [[ $QT_DIR == "$QT_ROOT/$QT_VERSION/$qt_subdir" ]] || die "QT_DIR=$QT_DIR has no lib/cmake/Qt6"
  info "Installing Qt $QT_VERSION $aqt_arch (aqt $AQT_VERSION) into $QT_ROOT"
  mkdir -p "$QT_ROOT"
  # Qt mirrors occasionally serve a truncated archive ("Bad offset for central directory"):
  # retry a few times before giving up.
  # Later attempts go to other official mirrors (a bad mirror keeps serving the same file).
  mirrors=("" "https://ftp.fau.de/qtproject/" "https://mirrors.ocf.berkeley.edu/qt/")
  for attempt in 1 2 3; do
    base=()
    [[ -n ${mirrors[attempt - 1]} ]] && base=(--base "${mirrors[attempt - 1]}")
    aqt install-qt "$aqt_host" desktop "$QT_VERSION" "$aqt_arch" -O "$QT_ROOT" "${base[@]}" && break
    ((attempt < 3)) || die "aqt failed to install Qt $QT_VERSION $aqt_arch"
    info "aqt failed (attempt $attempt), retrying with another mirror in 20 s"
    sleep 20
  done
fi
[[ -x $QT_DIR/bin/qmake ]] || die "$QT_DIR/bin/qmake missing"
echo "Qt: $QT_DIR"

# --- linuxdeploy ---------------------------------------------------------------------------------

fetch_tool() { # <repository> <tag> <file>
  local file=$LINUXDEPLOY_DIR/$3 expected=${SHA256[$3]}
  if [[ ! -x $file ]] || ! sha256sum --check --status <<<"$expected  $file"; then
    info "Downloading $3 ($2)"
    mkdir -p "$LINUXDEPLOY_DIR"
    curl -fsSL --retry 3 -o "$file.part" "https://github.com/$1/releases/download/$2/$3"
    if ! sha256sum --check --status <<<"$expected  $file.part"; then
      rm -f -- "$file.part"
      die "$3: SHA-256 mismatch (expected $expected)"
    fi
    chmod +x "$file.part"
    mv -f -- "$file.part" "$file"
  fi
}
fetch_tool linuxdeploy/linuxdeploy "$LINUXDEPLOY_TAG" "linuxdeploy-$ARCH.AppImage"
fetch_tool linuxdeploy/linuxdeploy-plugin-qt "$LINUXDEPLOY_QT_TAG" "linuxdeploy-plugin-qt-$ARCH.AppImage"
# Containers and CI runners have no FUSE: the AppImages extract themselves instead.
export APPIMAGE_EXTRACT_AND_RUN=1

# Hosts that register AppImages with binfmt_misc (e.g. NixOS with programs.appimage.binfmt)
# hand every AppImage to their interpreter, which does not exist inside a container: running
# one fails with "No such file or directory". Copies with the AppImage magic bytes zeroed
# ("AI\x02" at offset 8, unused by the runtime) run as the plain ELF executables they are.
runnable_copy() { # <appimage> <copy>
  cp -f -- "$1" "$2"
  printf '\0\0\0' | dd of="$2" bs=1 seek=8 count=3 conv=notrunc status=none
  chmod +x "$2"
}
TOOLS_DIR=$LINUXDEPLOY_DIR/runnable
mkdir -p "$TOOLS_DIR"
for tool in "linuxdeploy-$ARCH.AppImage" "linuxdeploy-plugin-qt-$ARCH.AppImage"; do
  runnable_copy "$LINUXDEPLOY_DIR/$tool" "$TOOLS_DIR/$tool"
done

# --- build + test --------------------------------------------------------------------------------

info "Configuring ($BUILD_DIR)"
cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_DIR" \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DBUILD_TESTING="$([[ ${BRULION_APPIMAGE_TESTS:-1} == 1 ]] && echo ON || echo OFF)" \
  -DBRULION_WERROR="${BRULION_WERROR:-ON}"

info "Building (-j$JOBS)"
cmake --build "$BUILD_DIR" --parallel "$JOBS"

if [[ ${BRULION_APPIMAGE_TESTS:-1} == 1 ]]; then
  info "Unit tests"
  QT_QPA_PLATFORM=offscreen ctest --test-dir "$BUILD_DIR" --output-on-failure --parallel "$JOBS"
fi

# --- AppDir ------------------------------------------------------------------------------------

info "Installing into $APPDIR"
rm -rf -- "$APPDIR"
DESTDIR=$(cd -- "$BUILD_DIR" && pwd)/AppDir cmake --install "$BUILD_DIR"
[[ -x $APPDIR/usr/bin/brulion ]] || die "cmake --install did not install usr/bin/brulion"
# The command line is reached through the app's binary (`Brulion.AppImage cli …`, docs/CLI.md):
# the AppImage has one entry point, so the separate brulion-cli stays out.
rm -f -- "$APPDIR/usr/bin/brulion-cli"

# Desktop entry and icon: the installed ones when the build provides them, else generated here.
desktop=$(find "$APPDIR/usr/share/applications" -name '*.desktop' -print -quit 2>/dev/null || true)
if [[ -z $desktop ]]; then
  desktop=$BUILD_DIR/$APP_ID.desktop
  cat >"$desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Brulion
GenericName=Markdown notes
GenericName[pl]=Notatki w Markdown
Comment=Offline-first Markdown notes: outliner, journals, calendar, reminders, calculations
Comment[pl]=Notatki w Markdown offline: konspekty, dziennik, kalendarz, przypomnienia, obliczenia
Exec=brulion --open %F
Icon=io.github.januszbit.brulion
Terminal=false
Categories=Office;Utility;
MimeType=text/markdown;text/x-markdown;text/plain;
Keywords=notes;markdown;outliner;journal;calendar;reminders;notatki;dziennik;kalendarz;przypomnienia;
StartupWMClass=brulion
EOF
fi
icon=$(find "$APPDIR/usr/share/icons" \( -name "$APP_ID.svg" -o -name "$APP_ID.png" \) -print -quit 2>/dev/null || true)
if [[ -z $icon ]]; then
  icon=$BUILD_DIR/$APP_ID.svg
  cp -- assets/logo.svg "$icon"
fi

# --- linuxdeploy ---------------------------------------------------------------------------------

mkdir -p "$DIST_DIR"
appimage=$DIST_DIR/Brulion-$VERSION-$ARCH.AppImage
# appimagetool runs zsyncmake in the current directory ($ROOT): it writes <name>.zsync here.
zsync=$(basename -- "$appimage").zsync
rm -f -- "$appimage" "$appimage.zsync" "$zsync"

info "Bundling Qt and packing $appimage"
(
  export QMAKE=$QT_DIR/bin/qmake
  export LD_LIBRARY_PATH=$QT_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
  # QML modules imported by our QML (compiled into the binary, so qmlimportscanner reads the sources).
  export QML_SOURCES_PATHS=$ROOT/src/app/qml
  # Wayland next to the default xcb: the platform plugin, and (through the module name) the
  # shell/decoration/graphics integration plugins. offscreen: headless runs and the check below.
  export EXTRA_PLATFORM_PLUGINS="libqwayland.so;libqoffscreen.so"
  export EXTRA_QT_MODULES="waylandcompositor"
  export LDAI_OUTPUT=$appimage LINUXDEPLOY_OUTPUT_VERSION=$VERSION LDAI_NO_APPSTREAM=1
  # The plugin finds linuxdeploy-plugin-qt next to linuxdeploy or on PATH.
  export PATH="$TOOLS_DIR:$PATH"
  "$TOOLS_DIR/linuxdeploy-$ARCH.AppImage" --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/brulion" --desktop-file "$desktop" --icon-file "$icon" \
    --plugin qt --output appimage
)
[[ -f $appimage ]] || die "linuxdeploy produced no $appimage"
chmod +x "$appimage"
# The update file belongs next to the AppImage: the release publishes the two together, and the
# .zsync refers to the AppImage by its file name.
if [[ -n ${LDAI_UPDATE_INFORMATION:-} ]]; then
  [[ -f $zsync ]] || die "no $zsync although LDAI_UPDATE_INFORMATION is set (is zsyncmake installed? --install-deps)"
  [[ $zsync -ef $appimage.zsync ]] || mv -f -- "$zsync" "$appimage.zsync"
fi

# --- check ---------------------------------------------------------------------------------------

info "Checking $appimage"
check_dir=$BUILD_DIR/appimage-check
rm -rf -- "$check_dir"
mkdir -p "$check_dir"
runnable_copy "$appimage" "$check_dir/brulion.AppImage"
(cd "$check_dir" && ./brulion.AppImage --appimage-extract >/dev/null)
missing=$(find "$check_dir/squashfs-root/usr" -type f \( -name '*.so*' -o -path '*/bin/brulion' \) -print0 |
  xargs -0 ldd 2>/dev/null | grep 'not found' | sort -u || true)
[[ -z $missing ]] || die "unresolved libraries in the AppImage:"$'\n'"$missing"
for plugin in platforms/libqxcb.so platforms/libqwayland.so platforms/libqoffscreen.so; do
  [[ -f $check_dir/squashfs-root/usr/plugins/$plugin ]] || die "plugin $plugin missing from the AppImage"
done
# "Otwórz za pomocą → Brulion" for Markdown and text files: the desktop entry AppImage integration
# tools (Gear Lever, AppImageLauncher, appimaged) install keeps the files' types and --open %F.
entry=$(find "$check_dir/squashfs-root" -maxdepth 1 -name '*.desktop' -print -quit)
[[ -n $entry ]] || die "no desktop entry at the top of the AppImage"
grep -q -- '^Exec=.* --open %F$' "$entry" || die "the AppImage's desktop entry does not open files (Exec … --open %F)"
grep -q '^MimeType=.*text/markdown;' "$entry" || die "the AppImage's desktop entry lists no Markdown files (MimeType)"
(cd "$check_dir/squashfs-root/usr/plugins" && printf 'plugins: %s\n' "$(echo *)")
# Start it headless: it must still be running after 10 s, without QML or plugin errors.
set +e
log=$(cd "$check_dir" && QT_QPA_PLATFORM=offscreen HOME=$PWD timeout 10 ./squashfs-root/AppRun 2>&1)
status=$?
set -e
printf '%s\n' "$log" | head -n 40
((status == 124)) || die "Brulion exited with status $status within 10 s"
if grep -E -i 'is not installed|module .* not found|cannot load library|could not load the qt platform plugin|failed to load' <<<"$log"; then
  die "Qt or QML errors at startup"
fi
rm -rf -- "$check_dir"

if [[ -n ${GITHUB_OUTPUT:-} ]]; then
  {
    echo "version=$VERSION"
    echo "appimage=$appimage"
  } >>"$GITHUB_OUTPUT"
fi

info "Done"
(
  cd "$DIST_DIR"
  sha256sum -- "$(basename -- "$appimage")"
  if [[ -f $zsync ]]; then sha256sum -- "$zsync"; fi
)
echo "AppImage: $(cd -- "$(dirname -- "$appimage")" && pwd)/$(basename -- "$appimage") ($(du -h "$appimage" | cut -f1))"
