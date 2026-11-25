# ai — AI for extensions (docs/EXTENSIONS.md "AI"): b.ai's requests to a model the user chose on
# the device — Anthropic's Messages API (Claude), any OpenAI-compatible chat API (OpenAI, a server
# of the user's own: Ollama, llama.cpp, LM Studio …) or a model that runs on the device itself
# (local.h: which models there are, their prompts and answers; localfiles.h: downloading their
# files; localrunner.h: what runs one, and loading it; cpus.h: the processor cores it computes on).
# Requests and answers as pure functions (tests); Client sends through
# sync::HttpTransport (no network in tests).
brulion_add_core_module(ai
    SOURCES
        ai.h ai.cpp
        local.h local.cpp local_p.h localformats.cpp
        gguf.h gguf.cpp
        ownmodels.h ownmodels.cpp
        localrunner.h localrunner.cpp
        localfiles.h localfiles.cpp
        cpus.h cpus.cpp
    DEPS util sync
    QT Network)

# The model runner (llamarunner.h: llama.cpp behind localrunner.h) and the plugin with it that
# the app loads on first use — only in builds with BRULION_LOCAL_AI (cmake/BrulionLocalAi.cmake).
# Neither is part of brulion_core: the app and brulion-cli link no llama.cpp. llama.cpp's own
# targets (llama, mtmd, ggml: 3rdparty/llama) are added after the core modules; their names
# resolve when the build files are generated.
if(BRULION_LOCAL_AI_ENABLED)
    find_package(Qt6 REQUIRED COMPONENTS Gui) # QImage: a picture's pixels for the model
    qt_add_library(brulion_ai_llama STATIC llamarunner.h llamarunner.cpp)
    target_link_libraries(brulion_ai_llama PUBLIC brulion_core_ai PRIVATE Qt6::Gui mtmd llama ggml)
    set_target_properties(brulion_ai_llama PROPERTIES POSITION_INDEPENDENT_CODE ON)
    brulion_set_warnings(brulion_ai_llama)

    add_library(brulion_localai MODULE localplugin.cpp)
    target_link_libraries(brulion_localai PRIVATE brulion_ai_llama Qt6::Core)
    brulion_set_warnings(brulion_localai)
endif()
