#!/usr/bin/env bash
# Build the LD_PRELOAD benchmark probe (probe/benchprobe.cpp) against the Qt of the dev shell.
#   tests/bench/app/build-probe.sh <out-dir>      (inside `nix develop`)
# Writes <out-dir>/libbenchprobe.so. A throw-away CMake project in <out-dir> (the repo's own
# build is not touched): Qt6 Core/Gui/GuiPrivate (QWindowSystemInterface)/Qml/Quick, AUTOMOC.
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
find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
add_library(benchprobe SHARED benchprobe.cpp)
target_compile_options(benchprobe PRIVATE -O2 -Wall)
target_link_libraries(benchprobe PRIVATE Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Qml Qt6::Quick)
CM
cmake -S "$out/src" -B "$out/build" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$out/build"
cp "$out/build/libbenchprobe.so" "$out/libbenchprobe.so"
echo "build-probe: $out/libbenchprobe.so"
