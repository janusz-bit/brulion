# llama.cpp (https://github.com/ggml-org/llama.cpp, MIT) runs the model on the device
# (src/core/ai/local.h, llamarunner.h; docs/EXTENSIONS.md "AI"). It is not vendored: the sources
# of one release (pin.json; LFM3 needs v0.8.0 or newer) are given as BRULION_LLAMA_SOURCE_DIR
# (cmake/BrulionLocalAi.cmake) and compiled here, without its programs, server, tests and
# download code — the libraries only:
#
#   llama, ggml, ggml-base   the model and its tensors
#   mtmd                     pictures for a model that sees (the projector, image → embeddings)
#   ggml-cpu-<variant>       the arithmetic, once per instruction set (AVX2, AVX-512 …; ARM: dot
#                            product, int8 matrix multiply …); the best one the processor has is
#                            loaded at run time
#
# Shared libraries with the processor code as plugins (llama.cpp's own GGML_BACKEND_DL layout),
# not one static blob: code compiled for AVX2 or ARMv8.2 then stays inside its plugin — linked
# statically, the linker may pick such a copy of a shared template for the whole program, which
# would then crash on an older processor without ever running a model. All of them go into one
# folder next to Brulion's plugin (brulion_localai, src/core/ai), which the app loads on first use:
# nothing here is loaded when the app starts.
#
# Sets (cache): BRULION_LLAMA_LIBRARIES (the targets to package) and BRULION_LLAMA_CPU_VARIANTS (the
# processor plugins to package, the best first — file names for the runner).
if(NOT BRULION_LOCAL_AI_ENABLED)
    return()
endif()

# Nothing of it is translated: lupdate (the target update_translations) reads none of its sources.
set_directory_properties(PROPERTIES QT_EXCLUDE_FROM_TRANSLATION ON)

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/pin.json _pin)
string(JSON _tag GET "${_pin}" tag)
# The sources are the pinned release's (a checkout of another one builds something else).
file(STRINGS "${BRULION_LLAMA_SOURCE_DIR}/CMakeLists.txt" _version_lines REGEX "^set\\(LLAMA_VERSION_(MAJOR|MINOR|PATCH) ")
string(REGEX REPLACE "[^0-9;]" "" _version "${_version_lines}")
string(REPLACE ";" "." _version "${_version}")
if(NOT "v${_version}" STREQUAL "${_tag}")
    message(FATAL_ERROR "BRULION_LLAMA_SOURCE_DIR has llama.cpp ${_version}, 3rdparty/llama/pin.json pins ${_tag}")
endif()

# llama.cpp's options (plain variables: its CMake keeps them, CMP0077).
set(BUILD_SHARED_LIBS ON)
set(LLAMA_BUILD_COMMON OFF)
set(LLAMA_BUILD_TESTS OFF)
set(LLAMA_BUILD_TOOLS OFF)
set(LLAMA_BUILD_EXAMPLES OFF)
set(LLAMA_BUILD_SERVER OFF)
set(LLAMA_BUILD_APP OFF)
set(LLAMA_BUILD_MTMD ON)        # pictures
set(LLAMA_SUBPROCESS OFF)       # (video through an ffmpeg process: not used)
set(MTMD_VIDEO OFF)
set(LLAMA_OPENSSL OFF)
set(LLAMA_ALL_WARNINGS OFF)
set(LLAMA_BUILD_IS_DEV OFF)
# No look at a git checkout for what llama.cpp says about its build: the same libraries from the
# same sources anywhere.
set(LLAMA_BUILD_NUMBER 0)
set(LLAMA_BUILD_COMMIT ${_tag})
set(GGML_BUILD_NUMBER 0)
set(GGML_BUILD_COMMIT ${_tag})
set(GGML_NATIVE OFF)            # never the build machine's own processor
set(GGML_BACKEND_DL ON)
set(GGML_CPU_ALL_VARIANTS ON)
set(GGML_OPENMP OFF)            # llama.cpp's own threads: no OpenMP runtime to package
set(GGML_CCACHE OFF)            # (ours: CMAKE_CXX_COMPILER_LAUNCHER)

# Compiled as llama.cpp compiles itself: its own C++ standard (not Brulion's C++20 without GNU
# extensions), no Qt code generators, no scan for C++ modules.
unset(CMAKE_CXX_STANDARD)
unset(CMAKE_CXX_STANDARD_REQUIRED)
unset(CMAKE_CXX_EXTENSIONS)
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# A debug build of Brulion still computes at full speed (a model at -O0 answers in minutes).
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT MSVC)
    add_compile_options(-O2)
endif()
# Its warnings are not ours.
if(MSVC)
    add_compile_options(/w)
else()
    add_compile_options(-w)
endif()
# Android: the same libraries in any directory (reproducible builds, as for Brulion's own code in
# the top-level CMakeLists.txt): no path of the sources in the debug information.
if(ANDROID)
    file(REAL_PATH "${BRULION_LLAMA_SOURCE_DIR}" _llama_real)
    add_compile_options("-ffile-prefix-map=${BRULION_LLAMA_SOURCE_DIR}=/llama.cpp")
    if(NOT _llama_real STREQUAL BRULION_LLAMA_SOURCE_DIR)
        add_compile_options("-ffile-prefix-map=${_llama_real}=/llama.cpp")
    endif()
endif()
# The C compiler's launcher as the C++ one's (ccache in the dev shell): llama.cpp is C and C++.
if(CMAKE_CXX_COMPILER_LAUNCHER AND NOT CMAKE_C_COMPILER_LAUNCHER)
    set(CMAKE_C_COMPILER_LAUNCHER ${CMAKE_CXX_COMPILER_LAUNCHER})
endif()

add_subdirectory("${BRULION_LLAMA_SOURCE_DIR}" llama.cpp EXCLUDE_FROM_ALL)

# The processor plugins worth packaging, the best first (the runner loads the first one the
# processor can run): not every variant llama.cpp can build.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$" AND ANDROID)
    # AVX2, and what every x86-64 Android has (SSE4.2): the emulator's processor has no AVX2
    # whatever the host's, nor has a Chromebook's Celeron.
    set(_wanted haswell sse42)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$" AND MSVC)
    # The same with MSVC (Windows), which builds no variant with BF16 (zen4) — its intrinsics need
    # /arch:AVX10.1: AVX-512 with VBMI and VNNI (Ice Lake, also what Zen 4 and Sapphire Rapids run)
    # in its place.
    set(_wanted icelake skylakex alderlake haswell)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    # AVX-512 with VNNI/VBMI/BF16 (Zen 4, Sapphire Rapids), AVX-512 (Skylake-X, Ice Lake),
    # AVX2 with VNNI (Alder Lake and later), AVX2 (2013 and later). Nothing older: too slow.
    set(_wanted zen4 skylakex alderlake haswell)
elseif(ANDROID)
    # int8 matrix multiply (2021 and later), dot product + half floats (2018 and later).
    set(_wanted android_armv8.6_1 android_armv8.2_2)
else()
    set(_wanted armv8.6_1 armv8.2_2 armv8.2_1 armv8.0_1)
endif()
set(_variants "")
foreach(_v IN LISTS _wanted)
    if(TARGET ggml-cpu-${_v})
        list(APPEND _variants ggml-cpu-${_v})
    endif()
endforeach()
if(NOT _variants)
    message(FATAL_ERROR "llama.cpp builds none of the processor plugins wanted here (${_wanted}) for ${CMAKE_SYSTEM_PROCESSOR}")
endif()
# llama.cpp's ggml depends on every variant it defines (14 on x86-64, 7 for Android on ARM), so all
# of them were compiled while only the ones above are packaged and loaded: each of the others is
# compiled from an empty file instead, into a folder of its own that nothing loads (x86-64: 10 of
# 14 variants, 40 sources each — some 450 s of compile time in every clean build). Without their
# instruction sets too (-march=armv9.2-a+sme …): a compiler older than llama.cpp's newest variants
# (the AppImage's on aarch64) need not know them.
set(_unused_dir ${CMAKE_CURRENT_BINARY_DIR}/unused-cpu-variants)
set(_dirs ${CMAKE_CURRENT_BINARY_DIR}/llama.cpp)
while(_dirs)
    list(POP_FRONT _dirs _dir)
    get_property(_targets DIRECTORY ${_dir} PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t IN LISTS _targets)
        get_target_property(_type ${_t} TYPE)
        if(_type STREQUAL "MODULE_LIBRARY" AND _t MATCHES "^ggml-cpu-" AND NOT _t IN_LIST _variants)
            set_property(TARGET ${_t} PROPERTY SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/unused-variant.cpp)
            set_property(TARGET ${_t} PROPERTY LINK_LIBRARIES "")
            set_property(TARGET ${_t} PROPERTY COMPILE_OPTIONS "")
            set_property(TARGET ${_t} PROPERTY COMPILE_DEFINITIONS "")
            set_target_properties(${_t} PROPERTIES
                LIBRARY_OUTPUT_DIRECTORY ${_unused_dir}
                RUNTIME_OUTPUT_DIRECTORY ${_unused_dir})
        endif()
    endforeach()
    get_property(_sub DIRECTORY ${_dir} PROPERTY SUBDIRECTORIES)
    list(APPEND _dirs ${_sub})
endwhile()

set(BRULION_LLAMA_LIBRARIES mtmd llama ggml ggml-base CACHE INTERNAL "llama.cpp's libraries to package")
set(BRULION_LLAMA_CPU_VARIANTS ${_variants} CACHE INTERNAL "llama.cpp's processor plugins to package, the best first")

# One folder for all of them in the build tree, as when installed (<lib>/brulion): each finds
# the others next to itself ($ORIGIN), with or without CMake's build-tree run paths (Nix: off).
# Windows looks for a DLL's own DLLs next to the program, not next to the DLL (and has no run
# paths): there they are built where the programs are — the build tree's top, where
# qt_standard_project_setup() puts every executable, the tests too — and packaged next to
# brulion.exe (scripts/windows-build.ps1).
if(WIN32)
    if(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set(_output_dir ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
    else()
        set(_output_dir ${CMAKE_BINARY_DIR})
    endif()
else()
    set(_output_dir ${CMAKE_BINARY_DIR}/lib/brulion)
endif()
set(BRULION_LOCAL_AI_OUTPUT_DIR ${_output_dir} CACHE INTERNAL "Where the model runner's libraries are built")
set_target_properties(${BRULION_LLAMA_LIBRARIES} ${BRULION_LLAMA_CPU_VARIANTS} PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${BRULION_LOCAL_AI_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${BRULION_LOCAL_AI_OUTPUT_DIR}
    BUILD_WITH_INSTALL_RPATH ON
    INSTALL_RPATH "$ORIGIN")
message(STATUS "Brulion: llama.cpp ${_tag} processor plugins: ${_variants}")

# Brulion's runner and plugin (src/core/ai/CMakeLists.txt, defined before this directory): the
# plugin is built into the same folder and loads the libraries from next to itself; the runner
# knows the processor plugins by their file names, the best first, and the release (a model's
# state it keeps on disk holds only for the llama.cpp that computed it). Brulion's plugins also
# link Qt, which a package that bundles it has one folder up, in <lib> (the AppImage's usr/lib):
# there a plugin finds it by itself too ($ORIGIN/..), not only as the library the app loaded.
set(BRULION_PLUGIN_RPATH "$ORIGIN;$ORIGIN/.." CACHE INTERNAL "The run path of Brulion's plugins over llama.cpp")
# In the build tree a plugin finds its Qt by CMake's build run path (the folders of what it links:
# aqt's Qt outside the system's folders — the AppImage's tests), and the installed one is written
# at install. With no build run paths (CMAKE_SKIP_BUILD_RPATH: Nix) and on Android (the APK takes
# the built files) the build tree has the installed one already.
if(UNIX AND NOT ANDROID AND NOT CMAKE_SKIP_BUILD_RPATH)
    set(BRULION_PLUGIN_BUILD_WITH_INSTALL_RPATH OFF CACHE INTERNAL "")
else()
    set(BRULION_PLUGIN_BUILD_WITH_INSTALL_RPATH ON CACHE INTERNAL "")
endif()
set(_variant_files "")
foreach(_v IN LISTS BRULION_LLAMA_CPU_VARIANTS)
    string(APPEND _variant_files "$<TARGET_FILE_NAME:${_v}>|")
endforeach()
target_compile_definitions(brulion_ai_llama PRIVATE "BRULION_LLAMA_CPU_VARIANTS=\"${_variant_files}\""
    "BRULION_LLAMA_TAG=\"${_tag}\"")
add_dependencies(brulion_ai_llama ${BRULION_LLAMA_CPU_VARIANTS})
set_target_properties(brulion_localai PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${BRULION_LOCAL_AI_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${BRULION_LOCAL_AI_OUTPUT_DIR}
    BUILD_WITH_INSTALL_RPATH ${BRULION_PLUGIN_BUILD_WITH_INSTALL_RPATH}
    INSTALL_RPATH "${BRULION_PLUGIN_RPATH}")

# The embedding model's runner and plugin for search by meaning (src/core/semantic: brulion_semantic),
# the same way: next to llama.cpp's libraries, the processor plugins known by their file names.
if(TARGET brulion_semantic_llama)
    target_compile_definitions(brulion_semantic_llama PRIVATE "BRULION_LLAMA_CPU_VARIANTS=\"${_variant_files}\"")
    add_dependencies(brulion_semantic_llama ${BRULION_LLAMA_CPU_VARIANTS})
    set_target_properties(brulion_semantic PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${BRULION_LOCAL_AI_OUTPUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${BRULION_LOCAL_AI_OUTPUT_DIR}
        BUILD_WITH_INSTALL_RPATH ${BRULION_PLUGIN_BUILD_WITH_INSTALL_RPATH}
        INSTALL_RPATH "${BRULION_PLUGIN_RPATH}")
endif()
