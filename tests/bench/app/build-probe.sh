#!/usr/bin/env bash
# Build the LD_PRELOAD benchmark probe (probe/benchprobe.cpp) against the Qt of the dev shell.
#   tests/bench/app/build-probe.sh <out-dir>      (inside `nix develop`)
# Writes <out-dir>/libbenchprobe.so. A throw-away CMake project in <out-dir> (the repo's own
# build is not touched): Qt6 Core/Gui/Qml/Quick, GuiPrivate (QWindowSystemInterface) and
# CorePrivate (the signal spy callbacks, QAbstractDeclarativeData::signalEmitted), AUTOMOC.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="$(mkdir -p "$1" && cd "$1" && pwd)"
mkdir -p "$out/src"
cp "$here/probe/benchprobe.cpp" "$out/src/"
cat > "$out/src/CMakeLists.txt" <<'CM'
cmake_minimum_required(VERSION 3.24)
project(benchprobe LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick)
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 REQUIRED COMPONENTS CorePrivate GuiPrivate)
# (QmlPrivate: which QML files the engine has loaded — the memory scenario; without it that count is 0.)
find_package(Qt6 QUIET COMPONENTS QmlPrivate)
# (QuickPrivate: an item's anchors, layer and palette in the objects' declaration sites.)
find_package(Qt6 QUIET COMPONENTS QuickPrivate)
add_library(benchprobe SHARED benchprobe.cpp)
target_compile_options(benchprobe PRIVATE -O2 -Wall)
target_link_libraries(benchprobe PRIVATE Qt6::Core Qt6::CorePrivate Qt6::Gui Qt6::GuiPrivate Qt6::Qml Qt6::Quick dl)
if(TARGET Qt6::QmlPrivate)
    target_link_libraries(benchprobe PRIVATE Qt6::QmlPrivate)
endif()
if(TARGET Qt6::QuickPrivate)
    target_link_libraries(benchprobe PRIVATE Qt6::QuickPrivate)
endif()
CM
cmake -S "$out/src" -B "$out/build" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$out/build"
cp "$out/build/libbenchprobe.so" "$out/libbenchprobe.so"
echo "build-probe: $out/libbenchprobe.so"
