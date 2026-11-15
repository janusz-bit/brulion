# Tests of the ai module (brulion_core_ai): b.ai's requests, the HTTP requests to Anthropic's and
# OpenAI-compatible APIs, their answers and errors — against a fake transport, no network.
brulion_add_test(tst_ai SOURCES tst_ai.cpp LIBS brulion_core_ai)
# The model on the device (ai/local.h): the models the app knows, the prompts (against what the
# model's own chat template gives: tests/data/ai/lfm3-prompts.json, written by lfm3_goldens.py),
# the answers read back and requests cut to fit — no model runs here.
brulion_add_test(tst_ai_local SOURCES tst_ai_local.cpp LIBS brulion_core_ai)
# The models' files (ai/localfiles.h): downloads against a small HTTP server on 127.0.0.1 — ranges,
# broken transfers, damaged files, cancel and remove.
brulion_add_test(tst_ai_localfiles SOURCES tst_ai_localfiles.cpp LIBS brulion_core_ai)

# With a model runner in the build (BRULION_LOCAL_AI): llama.cpp behind ai/llamarunner.h against a
# real model — skipped unless BRULION_LOCAL_MODEL names one — and the plugin loaded as the app
# loads it (found, loaded with llama.cpp's libraries from its folder; no model needed for that).
if(TARGET brulion_ai_llama)
    find_package(Qt6 REQUIRED COMPONENTS Gui)
    brulion_add_test(tst_ai_llama SOURCES tst_ai_llama.cpp LIBS brulion_ai_llama Qt6::Gui)
    brulion_add_test(tst_ai_localplugin SOURCES tst_ai_localplugin.cpp LIBS brulion_core_ai)
    add_dependencies(tst_ai_localplugin brulion_localai)
    foreach(_test tst_ai_llama tst_ai_localplugin)
        target_compile_definitions(${_test} PRIVATE BRULION_LOCAL_AI_OUTPUT_DIR="${BRULION_LOCAL_AI_OUTPUT_DIR}")
    endforeach()
    # llama.cpp's libraries are found where they were built, also without CMake's own build-tree
    # run paths (the Nix build switches those off), and Qt next to its core library.
    set_target_properties(tst_ai_llama PROPERTIES
        BUILD_WITH_INSTALL_RPATH ON
        INSTALL_RPATH "${BRULION_LOCAL_AI_OUTPUT_DIR};$<TARGET_FILE_DIR:Qt6::Core>")
    # A real model answers slowly on a busy machine.
    set_tests_properties(tst_ai_llama PROPERTIES TIMEOUT 1800)
endif()
