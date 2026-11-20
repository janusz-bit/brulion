# Speech recognition on the device (docs/UI.md "Voice notes", src/core/speech/speech.h): whisper.cpp
# compiled from the sources of one pinned release (3rdparty/whisper: pin.json) over llama.cpp's
# ggml — so only where the model on the device is built (cmake/BrulionLocalAi.cmake). The sources
# are not in the repository: BRULION_WHISPER_SOURCE_DIR names them — the Nix builds and the dev shell
# pass the pinned ones, the build scripts download and check them (scripts/llama-source.sh --whisper).
#
#   -DBRULION_SPEECH=auto   (default) on with the model on the device and the sources given
#   -DBRULION_SPEECH=ON     required (an error without them)
#   -DBRULION_SPEECH=OFF    never (no plugin brulion_speech: voice notes are recorded, not
#                           transcribed)
#
# Sets BRULION_SPEECH_ENABLED for src/core/speech (the recognizer and the plugin the app loads on
# first use), src/app (packaging) and the tests.
set(BRULION_SPEECH "auto" CACHE STRING "Speech recognition on the device, by whisper.cpp: auto, ON or OFF")
set(BRULION_WHISPER_SOURCE_DIR "$ENV{BRULION_WHISPER_SOURCE_DIR}" CACHE PATH
    "The sources of whisper.cpp for speech recognition (the release pinned in 3rdparty/whisper/pin.json)")

set(BRULION_SPEECH_ENABLED OFF)
if(BRULION_SPEECH STREQUAL "auto")
    if(BRULION_LOCAL_AI_ENABLED AND BRULION_WHISPER_SOURCE_DIR
       AND EXISTS "${BRULION_WHISPER_SOURCE_DIR}/include/whisper.h")
        set(BRULION_SPEECH_ENABLED ON)
    endif()
elseif(BRULION_SPEECH)
    if(NOT BRULION_LOCAL_AI_ENABLED)
        message(FATAL_ERROR "BRULION_SPEECH=ON needs the model on the device (BRULION_LOCAL_AI): whisper.cpp "
            "computes with llama.cpp's ggml")
    endif()
    if(NOT EXISTS "${BRULION_WHISPER_SOURCE_DIR}/include/whisper.h")
        message(FATAL_ERROR "BRULION_SPEECH=ON needs the sources of whisper.cpp: set BRULION_WHISPER_SOURCE_DIR "
            "(3rdparty/whisper/pin.json names the release; scripts/llama-source.sh --whisper downloads it)")
    endif()
    set(BRULION_SPEECH_ENABLED ON)
endif()
if(BRULION_SPEECH_ENABLED)
    message(STATUS "Brulion: speech recognition through whisper.cpp (${BRULION_WHISPER_SOURCE_DIR})")
else()
    message(STATUS "Brulion: no speech recognition (BRULION_SPEECH=${BRULION_SPEECH}, BRULION_WHISPER_SOURCE_DIR)")
endif()
