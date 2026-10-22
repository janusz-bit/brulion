cmake_minimum_required(VERSION 3.24)

project(Brulion
    VERSION 0.3.7
    DESCRIPTION "Offline-first Markdown notes: outliner + journals, calendar, reminders, calculations"
    HOMEPAGE_URL "https://github.com/janusz-bit/brulion"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BRULION_WERROR "Treat compiler warnings in Brulion's own code as errors" OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2 Svg LinguistTools)
qt_standard_project_setup(REQUIRES 6.8
    I18N_SOURCE_LANGUAGE en
    I18N_TRANSLATED_LANGUAGES pl)
qt_policy(SET QTP0004 NEW)

include(cmake/BrulionWarnings.cmake)

# Tests are built on desktop only (not for Android/WebAssembly cross builds).
if(NOT ANDROID AND NOT EMSCRIPTEN)
    include(CTest)
else()
    set(BUILD_TESTING OFF)
endif()

add_subdirectory(src/core)
add_subdirectory(src/platform)
add_subdirectory(src/app)
include(cmake/BrulionInstall.cmake)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
