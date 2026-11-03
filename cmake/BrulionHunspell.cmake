# brulion_find_hunspell(<var>) — the Hunspell library for spell checking (src/platform/spell_hunspell.cpp,
# docs/UI.md "Spell checking"): sets <var> to a target to link, or to "" when there is none.
#  1. the system's, through pkg-config (`hunspell`: the Nix package, Debian/Ubuntu's
#     libhunspell-dev for the AppImage);
#  2. else BRULION_HUNSPELL_SOURCE_DIR, an unpacked Hunspell release (hunspell-1.7.x: its
#     src/hunspell compiled here as the static library brulion_hunspell — Windows, where
#     scripts/windows-build.ps1 downloads it; Hunspell is MPL 1.1 / GPL 2+ / LGPL 2.1+, used here
#     under the LGPL). Warnings of this third-party code are off, as for 3rdparty/miniz.
function(brulion_find_hunspell var)
    if(TARGET brulion_hunspell)
        set(${var} brulion_hunspell PARENT_SCOPE)
        return()
    endif()
    if(TARGET PkgConfig::HUNSPELL)
        set(${var} PkgConfig::HUNSPELL PARENT_SCOPE)
        return()
    endif()
    if(NOT BRULION_HUNSPELL_SOURCE_DIR)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(HUNSPELL QUIET IMPORTED_TARGET GLOBAL hunspell)
        endif()
        if(TARGET PkgConfig::HUNSPELL)
            message(STATUS "Brulion: Hunspell ${HUNSPELL_VERSION} from pkg-config")
            set(${var} PkgConfig::HUNSPELL PARENT_SCOPE)
        else()
            set(${var} "" PARENT_SCOPE)
        endif()
        return()
    endif()

    set(_src "${BRULION_HUNSPELL_SOURCE_DIR}/src/hunspell")
    if(NOT EXISTS "${_src}/hunspell.cxx")
        message(FATAL_ERROR "BRULION_HUNSPELL_SOURCE_DIR=${BRULION_HUNSPELL_SOURCE_DIR} is no Hunspell release (no src/hunspell/hunspell.cxx)")
    endif()
    add_library(brulion_hunspell STATIC
        ${_src}/affentry.cxx ${_src}/affixmgr.cxx ${_src}/csutil.cxx ${_src}/filemgr.cxx ${_src}/hashmgr.cxx
        ${_src}/hunspell.cxx ${_src}/hunzip.cxx ${_src}/phonet.cxx ${_src}/replist.cxx ${_src}/suggestmgr.cxx)
    set_target_properties(brulion_hunspell PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(brulion_hunspell SYSTEM PUBLIC ${_src})
    target_compile_definitions(brulion_hunspell PUBLIC HUNSPELL_STATIC)
    target_compile_options(brulion_hunspell PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-w> $<$<CXX_COMPILER_ID:MSVC>:/w>)
    message(STATUS "Brulion: Hunspell compiled from ${BRULION_HUNSPELL_SOURCE_DIR}")
    set(${var} brulion_hunspell PARENT_SCOPE)
endfunction()
