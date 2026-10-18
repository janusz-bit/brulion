#!/usr/bin/env bash
# measure-copy.sh <source-tree> <copy-dir> — a *measurement copy* of a Brulion source tree for
# Android: the same app, plus
#   * android:debuggable="true" in the manifest (intent extras extraenvvars_* / applicationArguments
#     reach the app; run-as works) — never in a shipped APK;
#   * the app benchmark probe (tests/bench/app/probe/benchprobe.cpp + probe_android.cpp) built as
#     libbenchprobe.so and packaged with QT_ANDROID_EXTRA_LIBS, so Qt's loader loads it before
#     the app's main library. The app library itself is compiled exactly as in the source tree.
# The source tree is not modified. Re-running refreshes the copy (rsync) and re-applies the patch.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
bench=$(cd "$here/../.." && pwd) # tests/bench of the tree this script lives in
src=$(cd "$1" && pwd)
dst=$2
mkdir -p "$dst"
rsync -a --delete --exclude dist-android --exclude 'build*/' "$src/" "$dst/"

mkdir -p "$dst/tests/bench/app/probe" "$dst/tests/bench/mobile/android-probe"
cp "$bench/app/probe/benchprobe.cpp" "$dst/tests/bench/app/probe/"
cp "$here/probe_android.cpp" "$dst/tests/bench/mobile/android-probe/"

manifest=$dst/android/AndroidManifest.xml
grep -q 'android:debuggable="true"' "$manifest" ||
  sed -i '0,/<application/s//<application android:debuggable="true"/' "$manifest"
grep -q 'android:debuggable="true"' "$manifest" || { echo "measure-copy: could not patch $manifest" >&2; exit 1; }

cmake_file=$dst/src/app/CMakeLists.txt
sed -i '/^# --- Measurement copy only/,$d' "$cmake_file"
if ! grep -q 'benchprobe' "$cmake_file"; then
  cat >>"$cmake_file" <<'CMAKE'

# --- Measurement copy only (tests/bench/mobile/android-probe/measure-copy.sh) ------------------
# The app benchmark probe rides along as an extra library loaded before the app's main library.
if(ANDROID)
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
    add_library(benchprobe SHARED
        ${PROJECT_SOURCE_DIR}/tests/bench/app/probe/benchprobe.cpp
        ${PROJECT_SOURCE_DIR}/tests/bench/mobile/android-probe/probe_android.cpp)
    set_target_properties(benchprobe PROPERTIES AUTOMOC ON)
    target_compile_options(benchprobe PRIVATE -O2 -w)
    target_link_libraries(benchprobe PRIVATE Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Qml Qt6::Quick log)
    add_dependencies(brulion benchprobe)
    set_property(TARGET brulion APPEND PROPERTY QT_ANDROID_EXTRA_LIBS $<TARGET_FILE:benchprobe>)
endif()
CMAKE
fi
echo "measure-copy: $dst (debuggable, probe)"
