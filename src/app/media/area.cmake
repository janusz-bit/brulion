# Media area of the app (included by src/app/CMakeLists.txt in its scope, so relative paths are
# relative to src/app): audio and video in notes and voice notes (docs/UI.md "Audio and video",
# "Voice notes") — the brulion_media library (media/CMakeLists.txt), its QML registration
# (media/mediaqml.h: MediaPlayback, VoiceRecorder, Transcriber) and the views: the player of a media
# embed (qml/media/MediaEmbedTile.qml) with its controls (MediaControls.qml), the player view of a
# media file (qml/content/ContentMediaView.qml), the now-playing bar (MediaNowPlaying.qml), the
# recorder (VoiceRecorderDialog.qml) and the speech settings (MediaSpeechSettings.qml). The picture
# of a video comes from Qt Multimedia's VideoOutput (qml/media/video/MediaVideoSurface.qml) where the
# build plays video, else from a stand-in with the same interface (qml/media/novideo/) — the other
# file is never compiled into the module, so no QML imports QtMultimedia without it. Included
# before the shell's area (sorted): AppController links brulion_media for the Transcriber.
add_subdirectory(media)
target_link_libraries(brulion PRIVATE brulion_media)
# The generated QML type registration includes the media headers by file name.
target_include_directories(brulion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/media)
target_sources(brulion PRIVATE media/mediaqml.h)
if(BRULION_MULTIMEDIA_ENABLED)
    find_package(Qt6 REQUIRED COMPONENTS Multimedia)
    target_link_libraries(brulion PRIVATE Qt6::Multimedia)
    # qmlcachegen reads VideoOutput's type from Qt Multimedia's QML folder: where that folder is not
    # Qt's own (Nix: a store path per Qt module) it joins the module's import path, or
    # MediaVideoSurface.qml is left to the interpreter (tst_qml_compiled in the Nix sandbox).
    get_filename_component(_brulion_multimedia_qml "${Qt6Multimedia_DIR}/../../../${QT6_INSTALL_QML}" ABSOLUTE)
    get_filename_component(_brulion_qt_qml "${Qt6Qml_DIR}/../../../${QT6_INSTALL_QML}" ABSOLUTE)
    if(EXISTS "${_brulion_multimedia_qml}/QtMultimedia" AND NOT _brulion_multimedia_qml STREQUAL _brulion_qt_qml)
        set_property(TARGET brulion APPEND PROPERTY QT_QML_IMPORT_PATH "${_brulion_multimedia_qml}")
    endif()
    set(_brulion_video_surface qml/media/video/MediaVideoSurface.qml)
    # Android: Qt's own Android backend (the system's MediaPlayer and MediaRecorder: every format
    # Android plays) without Qt's FFmpeg backend, whose FFmpeg libraries would add 17 MB to each
    # ABI of the APK (docs/PERFORMANCE.md "Audio and video: what Qt Multimedia costs").
    # -DBRULION_ANDROID_FFMPEG=ON packages it (recorded voice notes then show their level as on the
    # desktop).
    option(BRULION_ANDROID_FFMPEG "Android: package Qt Multimedia's FFmpeg backend (+17 MB per ABI)" OFF)
    if(ANDROID AND NOT BRULION_ANDROID_FFMPEG AND TARGET Qt6::QFFmpegMediaPlugin)
        qt_import_plugins(brulion EXCLUDE Qt6::QFFmpegMediaPlugin)
    endif()
else()
    set(_brulion_video_surface qml/media/novideo/MediaVideoSurface.qml)
endif()
qt_target_qml_sources(brulion
    QML_FILES
        qml/media/MediaEmbedTile.qml
        qml/media/MediaControls.qml
        qml/media/MediaSlider.qml
        qml/media/MediaEmbedMenu.qml
        qml/media/MediaFullscreen.qml
        qml/media/MediaNowPlaying.qml
        qml/media/VoiceRecorderDialog.qml
        qml/media/MediaSpeechSettings.qml
        qml/content/ContentMediaView.qml
        ${_brulion_video_surface}
)
