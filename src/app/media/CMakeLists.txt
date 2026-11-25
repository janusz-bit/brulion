# brulion_media — audio and video in notes and voice notes, the app's C++ (docs/UI.md "Audio and
# video", "Voice notes"): the one player every media embed and the player view steer
# (mediaplayback.h: MediaPlayback, over mediaengine.h's engine and prober), the recorder
# (voicerecorder.h), speech recognition's queue and model files (transcriber.h: Transcriber, over
# core speech's plugin), text while recording (livetranscript.h) and the chat's dictation on the desktop (dictation.h: the two as
# platform/capture.h's speech recognizer). Qt Multimedia where the build has it (BRULION_MULTIMEDIA,
# cmake/BrulionMultimedia.cmake: the *_qt.cpp files), else the *_none.cpp files — nothing plays or
# records, WAV recordings can still be transcribed. A static library so tests/app/media links it
# without the app; the app's QML module registers its types through mediaqml.h.
if(BRULION_MULTIMEDIA_ENABLED)
    set(_brulion_media_backend mediaengine_qt.cpp voicerecorder_qt.cpp)
else()
    set(_brulion_media_backend mediaengine_none.cpp voicerecorder_none.cpp)
endif()
qt_add_library(brulion_media STATIC
    mediaengine.h
    mediaplayback.h mediaplayback.cpp
    voicerecorder.h voicerecorder_p.h voicerecorder.cpp
    transcriber.h transcriber.cpp
    livetranscript.h livetranscript.cpp
    dictation.h dictation.cpp
    ${_brulion_media_backend}
)
target_include_directories(brulion_media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_media
    PUBLIC Qt6::Core Qt6::Gui Qt6::Qml brulion_core_media brulion_core_speech brulion_core_ai brulion_platform)
if(BRULION_MULTIMEDIA_ENABLED)
    target_link_libraries(brulion_media PRIVATE Qt6::Multimedia)
    target_compile_definitions(brulion_media PUBLIC BRULION_MULTIMEDIA)
endif()
qt_extract_metatypes(brulion_media)
brulion_set_warnings(brulion_media)
