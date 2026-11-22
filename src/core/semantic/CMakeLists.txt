# semantic — search by meaning (docs/SEMANTIC.md, semantic/semantic.h). The module itself (chunks,
# calibration, vector index, fusion, the models, the plugin's interface) needs no llama.cpp and
# builds everywhere.
brulion_add_core_module(semantic
    SOURCES
        semantic.h
        chunker.h chunker.cpp
        calibration.h calibration.cpp
        vectorindex.h vectorindex.cpp
        fusion.h fusion.cpp
        models.h models.cpp
        notes.h notes.cpp
        semanticrunner.h semanticrunner.cpp
    DEPS ai markdown)

# The model runner (embedder.h: llama.cpp behind semanticrunner.h) and the plugin with it that the
# app and brulion-cli load on first use — only in builds with BRULION_LOCAL_AI
# (cmake/BrulionLocalAi.cmake), over the same pinned llama.cpp as the model on the device
# (3rdparty/llama). Neither is part of brulion_core: the app and brulion-cli link no llama.cpp.
# llama.cpp's own targets are added after the core modules; their names resolve when the build
# files are generated, and 3rdparty/llama/CMakeLists.txt gives the runner the processor plugins'
# names and puts the plugin next to them.
if(BRULION_LOCAL_AI_ENABLED)
    qt_add_library(brulion_semantic_llama STATIC embedder.h embedder.cpp)
    target_link_libraries(brulion_semantic_llama PUBLIC brulion_core_semantic PRIVATE llama ggml)
    set_target_properties(brulion_semantic_llama PROPERTIES POSITION_INDEPENDENT_CODE ON)
    brulion_set_warnings(brulion_semantic_llama)

    add_library(brulion_semantic MODULE semanticplugin.cpp)
    target_link_libraries(brulion_semantic PRIVATE brulion_semantic_llama Qt6::Core)
    brulion_set_warnings(brulion_semantic)
endif()
