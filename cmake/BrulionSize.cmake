# Smaller programs and libraries (docs/PERFORMANCE.md "Build times"): what the linker can leave out
# or share in an optimised build (Release, MinSizeRel, RelWithDebInfo) of every target, the vendored
# libraries included. Android's NDK, the browser's linker and MSVC already drop unused code.
#
#   Linux        each function and object in a section of its own, the ones nothing uses dropped
#                (-ffunction-sections -fdata-sections, --gc-sections)
#   lld          identical code folded where no address of it is compared (--icf=safe; the
#                address-significance tables come from Clang: -faddrsig, which Clang leaves out
#                on Android unless asked) — the C++ that qmlcachegen writes for the QML repeats
#                itself a lot. The Nix package links with lld, Android always does; with GNU ld
#                (the AppImage) nothing is folded
#   BRULION_PACK_RELOCATIONS   the relative relocations of a position-independent program packed
#                (RELR: -z pack-relative-relocs, a twentieth of the app's file); glibc 2.36 or newer
#                at run time, so off by default: the Nix package turns it on (the AppImage's glibc
#                is older; Android has its own: src/app/CMakeLists.txt)
#   brulion_order_startup(<target>)   Linux with lld: the functions a start, a tour of the views
#                and some typing run laid out first, and the compiled QML units of their files
#                (cmake/startup-order.txt, written by scripts/startup-order.py; --symbol-ordering-file).
#                A start runs a ninth of the program's code, spread all over it, and pages are read
#                and mapped whole: 32 MB of the 72 were resident after a start, 9 MB are
#                (docs/PERFORMANCE.md "Memory: only what is in use"). A name the program no longer
#                has is skipped: a stale file orders less. BRULION_STARTUP_ORDER=OFF: the linker's
#                own order
option(BRULION_PACK_RELOCATIONS
    "Pack the relative relocations (RELR; Linux, needs glibc 2.36 or newer at run time)" OFF)
option(BRULION_STARTUP_ORDER
    "Lay out the functions a start runs first (cmake/startup-order.txt; Linux with lld)" ON)

include(CheckCXXCompilerFlag)
include(CheckLinkerFlag)

function(brulion_enable_size_options)
    if(MSVC OR EMSCRIPTEN OR APPLE OR NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
        return()
    endif()
    set(_optimised "$<CONFIG:Release,MinSizeRel,RelWithDebInfo>")

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_compile_options("$<${_optimised}:-ffunction-sections>" "$<${_optimised}:-fdata-sections>")
        add_link_options("$<${_optimised}:LINKER:--gc-sections>")
    endif()

    # CMake 3.29 names the linker; an older one folds nothing.
    if(CMAKE_CXX_COMPILER_LINKER_ID STREQUAL "LLD")
        check_linker_flag(CXX "LINKER:--icf=safe" BRULION_HAVE_ICF_SAFE)
        if(BRULION_HAVE_ICF_SAFE)
            if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
                add_compile_options("$<${_optimised}:-faddrsig>")
            endif()
            add_link_options("$<${_optimised}:LINKER:--icf=safe>")
        endif()
    endif()

    if(BRULION_PACK_RELOCATIONS)
        if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR ANDROID)
            message(FATAL_ERROR "BRULION_PACK_RELOCATIONS is for Linux with glibc (Android packs its own)")
        endif()
        check_linker_flag(CXX "LINKER:-z,pack-relative-relocs" BRULION_HAVE_PACK_RELATIVE_RELOCS)
        if(NOT BRULION_HAVE_PACK_RELATIVE_RELOCS)
            message(FATAL_ERROR "BRULION_PACK_RELOCATIONS: the linker does not know -z pack-relative-relocs")
        endif()
        add_link_options("$<${_optimised}:LINKER:-z,pack-relative-relocs>")
    endif()
endfunction()

function(brulion_order_startup target)
    set(_order "${PROJECT_SOURCE_DIR}/cmake/startup-order.txt")
    if(NOT BRULION_STARTUP_ORDER OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux"
            OR NOT CMAKE_CXX_COMPILER_LINKER_ID STREQUAL "LLD" OR NOT EXISTS "${_order}")
        return()
    endif()
    # (Only where every function has a section of its own: brulion_enable_size_options().)
    set(_optimised "$<CONFIG:Release,MinSizeRel,RelWithDebInfo>")
    target_link_options(${target} PRIVATE
        "$<${_optimised}:LINKER:--symbol-ordering-file=${_order}>"
        "$<${_optimised}:LINKER:--no-warn-symbol-ordering>")
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${_order}")
endfunction()
