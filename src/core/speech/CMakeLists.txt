# speech — speech recognition on the device (docs/UI.md "Voice notes"): the Whisper models the app
# downloads (speech.h: files, sizes, licence; ai::LocalModelFiles downloads them), the languages it
# is asked to hear, what it heard (speech.h), and the recognizer behind an interface
# (speechrunner.h) that the plugin brulion_speech implements.
brulion_add_core_module(speech
    SOURCES
        speech.h speech.cpp
        speechrunner.h speechrunner.cpp
    DEPS ai media)

# The recognizer (whisperrunner.h: whisper.cpp behind speechrunner.h) and the plugin with it that
# the app loads on first use — only in builds with BRULION_SPEECH (cmake/BrulionSpeech.cmake). Neither
# is part of brulion_core: the app and brulion-cli link no whisper.cpp. whisper.cpp's library
# (brulion_whisper: 3rdparty/whisper) and llama.cpp's ggml are added after the core modules; their
# names resolve when the build files are generated.
if(BRULION_SPEECH_ENABLED)
    qt_add_library(brulion_speech_whisper STATIC whisperrunner.h whisperrunner.cpp)
    target_link_libraries(brulion_speech_whisper PUBLIC brulion_core_speech PRIVATE brulion_whisper ggml)
    set_target_properties(brulion_speech_whisper PROPERTIES POSITION_INDEPENDENT_CODE ON)
    brulion_set_warnings(brulion_speech_whisper)

    add_library(brulion_speech MODULE speechplugin.cpp)
    target_link_libraries(brulion_speech PRIVATE brulion_speech_whisper Qt6::Core)
    brulion_set_warnings(brulion_speech)
endif()
