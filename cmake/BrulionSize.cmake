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
option(BRULION_PACK_RELOCATIONS
    "Pack the relative relocations (RELR; Linux, needs glibc 2.36 or newer at run time)" OFF)

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
