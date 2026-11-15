# The model on the device (docs/EXTENSIONS.md "AI", src/core/ai/local.h): llama.cpp runs it, compiled
# from the sources of one pinned release (3rdparty/llama: pin.json, how it is built). The sources are
# not in the repository: BRULION_LLAMA_SOURCE_DIR names them — the Nix builds and the dev shell pass
# the pinned ones, the build scripts download and check them (scripts/llama-source.sh).
#
#   -DBRULION_LOCAL_AI=auto   (default) on when the sources are given, the app is built (not
#                             brulion-cli alone), 64-bit and not for the browser; else off — the
#                             app then has no "On this device"
#   -DBRULION_LOCAL_AI=ON     required (an error without the sources)
#   -DBRULION_LOCAL_AI=OFF    never (a smaller app: no llama.cpp libraries)
#
# Sets BRULION_LOCAL_AI_ENABLED for src/core/ai (the runner and the plugin the app loads on first
# use), src/app (packaging) and the tests.
set(BRULION_LOCAL_AI "auto" CACHE STRING "The model on the device, run by llama.cpp: auto, ON or OFF")
set(BRULION_LLAMA_SOURCE_DIR "$ENV{BRULION_LLAMA_SOURCE_DIR}" CACHE PATH
    "The sources of llama.cpp for the model on the device (the release pinned in 3rdparty/llama/pin.json)")

set(BRULION_LOCAL_AI_ENABLED OFF)
if(BRULION_LOCAL_AI STREQUAL "auto")
    if(BRULION_LLAMA_SOURCE_DIR AND EXISTS "${BRULION_LLAMA_SOURCE_DIR}/include/llama.h"
       AND BRULION_BUILD_APP AND NOT EMSCRIPTEN AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(BRULION_LOCAL_AI_ENABLED ON)
    endif()
elseif(BRULION_LOCAL_AI)
    if(NOT EXISTS "${BRULION_LLAMA_SOURCE_DIR}/include/llama.h")
        message(FATAL_ERROR "BRULION_LOCAL_AI=ON needs the sources of llama.cpp: set BRULION_LLAMA_SOURCE_DIR "
            "(3rdparty/llama/pin.json names the release; scripts/llama-source.sh downloads it)")
    endif()
    if(EMSCRIPTEN OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "BRULION_LOCAL_AI=ON: the model on the device needs a 64-bit build outside the browser")
    endif()
    set(BRULION_LOCAL_AI_ENABLED ON)
endif()
if(BRULION_LOCAL_AI_ENABLED)
    message(STATUS "Brulion: the model on the device through llama.cpp (${BRULION_LLAMA_SOURCE_DIR})")
    # llama.cpp is C and C++. Brulion itself enables only C++ (CMakeLists.txt): the C compiler is
    # set up here, at the top level — enabled only inside llama.cpp's directory, other directories
    # would know the language without its compiler.
    enable_language(C)
else()
    message(STATUS "Brulion: no model on the device (BRULION_LOCAL_AI=${BRULION_LOCAL_AI}, BRULION_LLAMA_SOURCE_DIR)")
endif()
