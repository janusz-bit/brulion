# The speech module (src/core/speech): the Whisper models the app knows, the languages, what is
# kept of what a recognizer heard — no recognizer runs here.
brulion_add_test(tst_speech SOURCES tst_speech.cpp LIBS brulion_core_speech)

# With the recognizer in the build (BRULION_SPEECH): the plugin brulion_speech loaded as the app
# loads it (found, loaded with ggml from its folder, a missing model an error), and whisper.cpp
# hearing a recording — skipped unless BRULION_SPEECH_MODEL names a Whisper model (a ggml .bin);
# BRULION_SPEECH_SAMPLE a WAV of speech, BRULION_SPEECH_EXPECT words it says (the test's comment).
if(TARGET brulion_speech)
    brulion_add_test(tst_speech_plugin SOURCES tst_speech_plugin.cpp LIBS brulion_core_speech)
    add_dependencies(tst_speech_plugin brulion_speech)
    target_compile_definitions(tst_speech_plugin PRIVATE BRULION_LOCAL_AI_OUTPUT_DIR="${BRULION_LOCAL_AI_OUTPUT_DIR}")
    # A real model hears slowly on a busy machine.
    set_tests_properties(tst_speech_plugin PROPERTIES TIMEOUT 1800)
endif()
