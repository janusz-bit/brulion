cmake_minimum_required(VERSION 3.24)

project(Brulion
    VERSION 0.4.0
    DESCRIPTION "Offline-first Markdown notes: outliner + journals, calendar, reminders, calculations"
    HOMEPAGE_URL "https://github.com/janusz-bit/brulion"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BRULION_WERROR "Treat compiler warnings in Brulion's own code as errors" OFF)
# OFF: only the core libraries and the command line brulion-cli (src/cli), without Qt Quick, QML,
# SVG or Wayland — for headless machines (nix: packages.<system>.brulion-cli). Desktop only.
option(BRULION_BUILD_APP "Build the app (Qt Quick); OFF builds the core libraries and brulion-cli only" ON)

if(BRULION_BUILD_APP)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2 Svg LinguistTools)
else()
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network)
endif()
qt_standard_project_setup(REQUIRES 6.8
    I18N_SOURCE_LANGUAGE en
    I18N_TRANSLATED_LANGUAGES pl)
if(BRULION_BUILD_APP)
    qt_policy(SET QTP0004 NEW)
endif()

include(cmake/BrulionWarnings.cmake)

# Tests are built on desktop only (not for Android/WebAssembly cross builds).
if(NOT ANDROID AND NOT EMSCRIPTEN)
    include(CTest)
else()
    set(BUILD_TESTING OFF)
endif()

add_subdirectory(src/core)
# The desktop (Linux): the single-instance socket and the command line for scripts and AI agents
# (brulion-cli, docs/CLI.md); the app hands `brulion cli …` over to it.
if(NOT ANDROID AND NOT EMSCRIPTEN)
    add_subdirectory(src/ipc)
    add_subdirectory(src/cli)
elseif(NOT BRULION_BUILD_APP)
    message(FATAL_ERROR "BRULION_BUILD_APP=OFF builds brulion-cli, which is for the desktop only")
endif()
if(BRULION_BUILD_APP)
    add_subdirectory(src/platform)
    add_subdirectory(src/app)
    include(cmake/BrulionInstall.cmake)
endif()

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
