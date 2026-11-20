# Qt Multimedia for audio and video in notes and voice notes (docs/UI.md "Audio and video", "Voice
# notes"): the player of `![[recording.mp3]]` and `![[video.mp4]]`, the player view of a media file,
# the recorder. Qt's FFmpeg backend on Linux, Windows and Android (Android: also Qt's own Android
# backend), the browser's elements in WebAssembly.
#
#   -DBRULION_MULTIMEDIA=auto   (default) on when Qt has the module (the app only)
#   -DBRULION_MULTIMEDIA=ON     required (an error without it)
#   -DBRULION_MULTIMEDIA=OFF    never: media embeds are file chips with "Open in another app",
#                               no recorder (src/app/media/mediabackend_none.cpp)
#
# Sets BRULION_MULTIMEDIA_ENABLED for src/app/media (and its QML) and the tests.
set(BRULION_MULTIMEDIA "auto" CACHE STRING "Audio and video players and the voice recorder (Qt Multimedia): auto, ON or OFF")
set(BRULION_MULTIMEDIA_ENABLED OFF)
if(BRULION_BUILD_APP)
    if(BRULION_MULTIMEDIA STREQUAL "auto")
        find_package(Qt6 QUIET COMPONENTS Multimedia)
        if(TARGET Qt6::Multimedia)
            set(BRULION_MULTIMEDIA_ENABLED ON)
        endif()
    elseif(BRULION_MULTIMEDIA)
        find_package(Qt6 REQUIRED COMPONENTS Multimedia)
        set(BRULION_MULTIMEDIA_ENABLED ON)
    endif()
endif()
if(BRULION_MULTIMEDIA_ENABLED)
    message(STATUS "Brulion: audio and video through Qt Multimedia ${Qt6Multimedia_VERSION}")
else()
    message(STATUS "Brulion: no audio and video player (BRULION_MULTIMEDIA=${BRULION_MULTIMEDIA}): \"Open in another app\"")
endif()
