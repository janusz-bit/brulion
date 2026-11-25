# whisper.cpp (https://github.com/ggml-org/whisper.cpp, MIT) recognizes speech on the device
# (src/core/speech: speechrunner.h, whisperrunner.h; docs/UI.md "Voice notes"). It is not vendored:
# the sources of one release (pin.json) are given as BRULION_WHISPER_SOURCE_DIR
# (cmake/BrulionSpeech.cmake) and only its library is compiled here — src/whisper.cpp and the
# Apple Neural Engine stub it always links —, without its own ggml, programs, tests or bindings: it
# computes with llama.cpp's ggml (3rdparty/llama: ggml, ggml-base and the processor plugins),
# loaded once for both. The library is static and position independent: it goes into the plugin
# brulion_speech (src/core/speech), which the app loads the first time it transcribes.
#
# Sets the target brulion_whisper.
if(NOT BRULION_SPEECH_ENABLED)
    return()
endif()

# Nothing of it is translated: lupdate (the target update_translations) reads none of its sources.
set_directory_properties(PROPERTIES QT_EXCLUDE_FROM_TRANSLATION ON)

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/pin.json _pin)
string(JSON _tag GET "${_pin}" tag)
# The sources are the pinned release's (a checkout of another one builds something else).
file(STRINGS "${BRULION_WHISPER_SOURCE_DIR}/CMakeLists.txt" _version_lines
    REGEX "^set\\(WHISPER_VERSION_(MAJOR|MINOR|PATCH) ")
string(REGEX REPLACE "[^0-9;]" "" _version "${_version_lines}")
string(REPLACE ";" "." _version "${_version}")
if(NOT "v${_version}" STREQUAL "${_tag}")
    message(FATAL_ERROR "BRULION_WHISPER_SOURCE_DIR has whisper.cpp ${_version}, 3rdparty/whisper/pin.json pins ${_tag}")
endif()

set(_src ${BRULION_WHISPER_SOURCE_DIR}/src)
add_library(brulion_whisper STATIC
    ${BRULION_WHISPER_SOURCE_DIR}/include/whisper.h
    ${_src}/whisper-arch.h
    ${_src}/whisper.cpp
    ${_src}/aneforge/whisper-aneforge.h
    ${_src}/aneforge/whisper-aneforge.cpp)
target_include_directories(brulion_whisper PUBLIC ${BRULION_WHISPER_SOURCE_DIR}/include PRIVATE ${_src})
find_package(Threads REQUIRED)
target_link_libraries(brulion_whisper PUBLIC ggml Threads::Threads)
target_compile_definitions(brulion_whisper PRIVATE "WHISPER_VERSION=\"${_version}\"")
if(WIN32)
    target_compile_definitions(brulion_whisper PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
# Compiled as whisper.cpp compiles itself: its own C++ standard (C++17 is enough), no Qt code
# generators, no scan for C++ modules.
set_target_properties(brulion_whisper PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 17
    CXX_EXTENSIONS OFF
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
    CXX_SCAN_FOR_MODULES OFF)
# A debug build of Brulion still computes at full speed (a minute of speech at -O0 takes minutes).
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT MSVC)
    target_compile_options(brulion_whisper PRIVATE -O2)
endif()
# Its warnings are not ours.
if(MSVC)
    target_compile_options(brulion_whisper PRIVATE /w)
else()
    target_compile_options(brulion_whisper PRIVATE -w)
endif()
# Android: the same library in any directory (reproducible builds): no path of the sources.
if(ANDROID)
    file(REAL_PATH "${BRULION_WHISPER_SOURCE_DIR}" _whisper_real)
    target_compile_options(brulion_whisper PRIVATE "-ffile-prefix-map=${BRULION_WHISPER_SOURCE_DIR}=/whisper.cpp")
    if(NOT _whisper_real STREQUAL BRULION_WHISPER_SOURCE_DIR)
        target_compile_options(brulion_whisper PRIVATE "-ffile-prefix-map=${_whisper_real}=/whisper.cpp")
    endif()
endif()
message(STATUS "Brulion: whisper.cpp ${_tag} over llama.cpp's ggml")

# Brulion's recognizer and plugin (src/core/speech/CMakeLists.txt, defined before this directory):
# the plugin goes next to llama.cpp's libraries and the model runner's plugin (one folder, each
# finding the others next to itself) and knows ggml's processor plugins by their file names, as
# the model runner does.
set(_variant_files "")
foreach(_v IN LISTS BRULION_LLAMA_CPU_VARIANTS)
    string(APPEND _variant_files "$<TARGET_FILE_NAME:${_v}>|")
endforeach()
target_compile_definitions(brulion_speech_whisper PRIVATE "BRULION_LLAMA_CPU_VARIANTS=\"${_variant_files}\"")
add_dependencies(brulion_speech_whisper ${BRULION_LLAMA_CPU_VARIANTS})
set_target_properties(brulion_speech PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${BRULION_LOCAL_AI_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${BRULION_LOCAL_AI_OUTPUT_DIR}
    BUILD_WITH_INSTALL_RPATH ${BRULION_PLUGIN_BUILD_WITH_INSTALL_RPATH}
    INSTALL_RPATH "${BRULION_PLUGIN_RPATH}")
