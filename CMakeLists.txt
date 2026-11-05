cmake_minimum_required(VERSION 3.24)

project(Brulion
    VERSION 0.11.1
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
# OFF: the app never asks GitHub for a newer version (src/app/shell/appupdates.h) and has neither
# the message about one nor its switch in the settings — for builds that a store updates (F-Droid,
# IzzyOnDroid; docs/ANDROID.md). Android and Windows only ask with ON (the default).
option(BRULION_UPDATE_CHECK "Ask GitHub for a newer version (Android, Windows); OFF for store builds" ON)

if(BRULION_BUILD_APP)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2 Svg LinguistTools)
else()
    # LinguistTools: brulion-cli's Polish messages (src/cli/CMakeLists.txt); without it English.
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network OPTIONAL_COMPONENTS LinguistTools)
endif()
qt_standard_project_setup(REQUIRES 6.8
    I18N_SOURCE_LANGUAGE en
    I18N_TRANSLATED_LANGUAGES pl)
if(BRULION_BUILD_APP)
    qt_policy(SET QTP0004 NEW)
endif()

include(cmake/BrulionWarnings.cmake)
include(cmake/BrulionWindows.cmake)

# The desktop (Linux and Windows): the system tray, one instance per session, the command line
# brulion-cli and the self-tests of the real app (BRULION_DESKTOP_INTEGRATION in the code).
if(NOT ANDROID AND NOT EMSCRIPTEN AND (WIN32 OR (UNIX AND NOT APPLE)))
    set(BRULION_DESKTOP ON)
else()
    set(BRULION_DESKTOP OFF)
endif()

# Windows (MSVC), every target including 3rdparty: UTF-8 sources (Polish string literals), the
# standard's rules and a correct __cplusplus, more sections per object file than the default
# (generated QML type registrations, big test files), no min/max macros from <windows.h>.
if(MSVC)
    add_compile_options(/utf-8 /permissive- /Zc:__cplusplus /bigobj)
    add_compile_definitions(NOMINMAX)
endif()

# Android: the same sources give the same APK in any directory (reproducible builds — F-Droid
# compares its build with the published APK, docs/ANDROID.md): the NDK compiles with -g, so the
# paths of the checkout, the build directory, the NDK and Qt would end up in the debug info (and
# through it in the libraries' build ID, which stays after Gradle strips them).
if(ANDROID)
    set(_brulion_path_maps "${PROJECT_SOURCE_DIR}=/brulion" "${PROJECT_BINARY_DIR}=/brulion-build")
    if(CMAKE_ANDROID_NDK)
        list(APPEND _brulion_path_maps "${CMAKE_ANDROID_NDK}=/ndk")
    elseif(ANDROID_NDK)
        list(APPEND _brulion_path_maps "${ANDROID_NDK}=/ndk")
    endif()
    if(QT6_INSTALL_PREFIX)
        list(APPEND _brulion_path_maps "${QT6_INSTALL_PREFIX}=/qt")
    endif()
    if(QT_HOST_PATH)
        list(APPEND _brulion_path_maps "${QT_HOST_PATH}=/qt-host")
    endif()
    # Each path also as its real path (the compiler sees the NDK through the real path of its
    # clang, e.g. in a symlinked SDK).
    foreach(_map IN LISTS _brulion_path_maps)
        string(REGEX MATCH "^(.*)=([^=]*)$" _match "${_map}")
        set(_from "${CMAKE_MATCH_1}")
        set(_to "${CMAKE_MATCH_2}")
        file(REAL_PATH "${_from}" _real)
        add_compile_options("-ffile-prefix-map=${_from}=${_to}")
        if(NOT _real STREQUAL _from)
            add_compile_options("-ffile-prefix-map=${_real}=${_to}")
        endif()
    endforeach()
endif()

# Tests are built on desktop only (not for Android/WebAssembly cross builds).
if(NOT ANDROID AND NOT EMSCRIPTEN)
    include(CTest)
else()
    set(BUILD_TESTING OFF)
endif()

add_subdirectory(src/core)
# The desktop (Linux, Windows): the single-instance socket and the command line for scripts and AI
# agents (brulion-cli, docs/CLI.md); the app hands `brulion cli …` over to it.
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
